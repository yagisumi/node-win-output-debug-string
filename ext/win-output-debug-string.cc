#define NAPI_VERSION 4
#include <napi.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <cstdio>

#if DEBUG
void
debug(const char *fmt, ...)
{
    char buf[128] = {0};
    va_list list;
    va_start(list, fmt);
    _vsnprintf(buf, 127, fmt, list);
    OutputDebugStringA(buf);
    va_end(list);
}
#else
#define debug(fmt, ...)
#endif

using namespace Napi;

static ObjectReference monitor;

static const size_t STR_LEN = 4096 - sizeof(DWORD);

struct OdsBuffer {
  DWORD processId;
  char  data[STR_LEN];
};

enum ErrorCode {
  OK,
  ArgumentError,
  AlreadyStartingError,
  SecurityInitializationError,
  ResourcesInitializationError,
  AlreadyExistsError,

};

class Monitor : public ObjectWrap<Monitor> {
  public:
    static Object Init(Napi::Env env, Object exports);
    static FunctionReference constructor;
    Monitor(const CallbackInfo &info);
    virtual ~Monitor();
  private:
    Napi::Value start(const CallbackInfo &info);
    Napi::Value stop(const CallbackInfo &info);

    bool running = false;
    bool securityInitialized = false;
    HANDLE file;
    OdsBuffer* buf;
    WCHAR wstr[STR_LEN];
    SECURITY_ATTRIBUTES secAttr;
    SECURITY_DESCRIPTOR secDesc;
    HANDLE BUFFER_READY;
    HANDLE DATA_READY;
    ThreadSafeFunction tsfn;
    std::thread nativeThread;
    bool initializeSecurity();
    ErrorCode initializeResources();
    void destroyResources();
    Napi::Value error(const CallbackInfo &info, ErrorCode errCode);
    Napi::Value ok(const CallbackInfo &info);
};

FunctionReference Monitor::constructor;

Monitor::Monitor(const Napi::CallbackInfo &info) : Napi::ObjectWrap<Monitor>(info) {}

Monitor::~Monitor() {
  debug("Monitor::~Monitor()");
}

bool Monitor::initializeSecurity() {
  secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secAttr.bInheritHandle = TRUE;
  secAttr.lpSecurityDescriptor = &secDesc;

  if (!InitializeSecurityDescriptor(&secDesc, SECURITY_DESCRIPTOR_REVISION))
    return false;

  if (!SetSecurityDescriptorDacl(&secDesc, TRUE, (PACL)NULL, FALSE))
    return false;

  return true;
}

Value Monitor::ok(const CallbackInfo &info) {
  auto env = info.Env();
  auto obj = Object::New(env);
  obj["ok"] = Boolean::New(env, true);

  return obj;
}

Value Monitor::error(const CallbackInfo &info, ErrorCode errCode) {
  auto env = info.Env();
  auto obj = Object::New(env);

  if (errCode == OK) {
    obj["ok"] = Boolean::New(env, true);
  } else {
    auto err = Object::New(env);
    obj["ok"] = Boolean::New(env, false);
    const char *errName;
    const char *errMsg;

    if (errCode == ArgumentError) {
      errName = "ArgumentError";
      errMsg = "Argument should be a callback function.";
    } else if (errCode == AlreadyStartingError) {
      errName = "AlreadyStartingError";
      errMsg = "Already Starting.";
    } else if (errCode == SecurityInitializationError) {
      errName = "SecurityInitializationError";
      errMsg = "An error occurred during security initialization.";
    } else if (errCode == ResourcesInitializationError) {
      errName = "ResourcesInitializationError";
      errMsg = "An error occurred during resources initialization.";
    } else if (errCode == AlreadyExistsError) {
      errName = "AlreadyExistsError";
      errMsg = "Event already used.";
    } else {
      errName = "unexpected";
      errMsg = "unexpected";
    }

    err["name"] = String::New(env, errName);
    err["message"] = String::New(env, errMsg);
    obj["error"] = err;
  }

  return obj;
}

Value Monitor::start(const CallbackInfo &info) {
  if (running) {
    return error(info, AlreadyStartingError);
  }

  if (info.Length() == 0 || !info[0].IsFunction()) {
    return error(info, ArgumentError);
  }
  Function func = info[0].As<Function>();

  if (!securityInitialized) {
    securityInitialized = initializeSecurity();
    if (!securityInitialized) {
      return error(info, SecurityInitializationError);
    }
  }

  ErrorCode err = initializeResources();
  if (err != OK) {
    destroyResources();
    return error(info, err);
  }

  // Napi::Env env = info.Env();
  tsfn = ThreadSafeFunction::New(
    info.Env(),
    func,
    "monitor",
    0,
    1,
    [this](Napi::Env) {
      this->nativeThread.join();
    }
  );

  running = true;
  nativeThread = std::thread([this]() {
    auto callback = [this]( Napi::Env env, Function jsCallback ) {
      MultiByteToWideChar(CP_ACP, 0, this->buf->data, -1, wstr, STR_LEN);
      auto obj = Object::New(env);
      obj["pid"] = Number::New(env, this->buf->processId);
      obj["message"] = String::New(env, reinterpret_cast<const char16_t*>(this->wstr));

      jsCallback.Call({ obj });
    };

    DWORD r;
    SetEvent(this->BUFFER_READY);

    while (this->running) {
      r = WaitForSingleObject(this->DATA_READY, INFINITE);
      if (r != WAIT_OBJECT_0)
        break;

      if (!this->running)
        break;

      napi_status status = this->tsfn.NonBlockingCall(callback);
      std::printf("status: %d, after NonBlockingCall\n", status);
      SetEvent(BUFFER_READY);
    }
    tsfn.Release();
    this->destroyResources();
    this->running = false;
  });

  return ok(info);
}

ErrorCode Monitor::initializeResources() {
  file = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*buf), TEXT("DBWIN_BUFFER"));
  if (file == INVALID_HANDLE_VALUE) {
    return ResourcesInitializationError;
  }

  buf = static_cast<OdsBuffer *>(MapViewOfFile(file, SECTION_MAP_READ, 0, 0, 0));
  if (buf == nullptr) {
    return ResourcesInitializationError;
  }

  BUFFER_READY = CreateEvent(&secAttr, FALSE, FALSE, TEXT("DBWIN_BUFFER_READY"));
  if (BUFFER_READY == NULL) {
    return ResourcesInitializationError;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return AlreadyExistsError;
  }

  DATA_READY = CreateEvent(&secAttr, FALSE, FALSE, TEXT("DBWIN_DATA_READY"));
  if (DATA_READY == NULL) {
    return ResourcesInitializationError;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return AlreadyExistsError;
  }

  return OK;
}

void Monitor::destroyResources() {
  if (DATA_READY != NULL) {
    CloseHandle(DATA_READY);
    DATA_READY = NULL;
  }

  if (BUFFER_READY != NULL) {
    CloseHandle(BUFFER_READY);
    BUFFER_READY = NULL;
  }

  if (buf != nullptr) {
    UnmapViewOfFile(buf);
    buf = nullptr;
  }

  if (file != INVALID_HANDLE_VALUE && file != 0) {
    CloseHandle(file);
    file = 0;
  }
}

Value Monitor::stop(const CallbackInfo &info) {
  running = false;
  if (DATA_READY) {
    SetEvent(DATA_READY);
    return Boolean::New(info.Env(), true);
  }
  return Boolean::New(info.Env(), false);
}


Object Monitor::Init(Napi::Env env, Object exports) {
  debug("Monitor::Init");
  Function func = DefineClass(env, "Monitor", {
    InstanceMethod("start", &Monitor::start),
    InstanceMethod("stop", &Monitor::stop),
  });

  constructor = Persistent(func);
  constructor.SuppressDestruct();

  if (monitor == nullptr) {
    auto instance = constructor.New({});
    monitor = Persistent(instance);
    monitor.SuppressDestruct();
  }

  exports["monitor"] = monitor.Value();
  return exports;
}

Value outputDebugString(const CallbackInfo& info) {
  if (info.Length() == 1 && info[0].IsString()) {
    auto str = info[0].As<String>().Utf16Value();
    OutputDebugStringW(reinterpret_cast<const wchar_t *>(str.c_str()));
  }

  return info.Env().Undefined();
}

Object Init(Env env, Object exports) {
  exports["OutputDebugString"] = Function::New(env, outputDebugString);
  Monitor::Init(env, exports);
  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
