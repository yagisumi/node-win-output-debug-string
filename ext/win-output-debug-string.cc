#define NAPI_VERSION 4
#include <napi.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <cstdio>

using namespace Napi;

static const size_t STR_LEN = 4096 - sizeof(DWORD);

struct OdsBuffer {
  DWORD pid;
  char  message[STR_LEN];
};

class OdsInfo {
  public:
    DWORD pid;
    WCHAR message[STR_LEN];
    OdsInfo(OdsBuffer *buf) {
      pid = buf->pid;
      MultiByteToWideChar(CP_ACP, 0, buf->message, -1, message, STR_LEN);
    }
};

enum ErrorCode {
  OK,
  ArgumentError,
  AlreadyStartingError,
  SecurityInitializationError,
  ResourcesInitializationError,
  EventAlreadyExistsError,
};

static ObjectReference monitor;

class Monitor : public ObjectWrap<Monitor> {
  public:
    static Object Init(Napi::Env env, Object exports);
    static FunctionReference constructor;
    Monitor(const CallbackInfo &info);
    Napi::Value start(const CallbackInfo &info);
    Napi::Value stop(const CallbackInfo &info);

  private:
    bool running = false;
    bool securityInitialized = false;
    HANDLE file = NULL;
    OdsBuffer* buf = nullptr;
    SECURITY_ATTRIBUTES secAttr;
    SECURITY_DESCRIPTOR secDesc;
    HANDLE BUFFER_READY = NULL;
    HANDLE DATA_READY = NULL;
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
    obj["ok"] = Boolean::New(env, false);
    auto err = Object::New(env);
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
    } else if (errCode == EventAlreadyExistsError) {
      errName = "EventAlreadyExistsError";
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
    auto callback = []( Napi::Env env, Function jsCallback, OdsInfo* info) {
      auto obj = Object::New(env);
      obj["pid"] = Number::New(env, info->pid);
      obj["message"] = String::New(env, reinterpret_cast<const char16_t*>(info->message));
      delete info;

      jsCallback.Call({ obj });
    };

    DWORD r;
    SetEvent(this->BUFFER_READY);

    while (this->running) {
      r = WaitForSingleObject(this->DATA_READY, INFINITE);
      if (r != WAIT_OBJECT_0) {
        break;
      }
      if (!this->running) {
        break;
      }

      OdsInfo* info = new OdsInfo(this->buf);
      napi_status status = this->tsfn.NonBlockingCall(info, callback);
      if (status != napi_ok) {
        delete info;
      }
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
    return EventAlreadyExistsError;
  }

  DATA_READY = CreateEvent(&secAttr, FALSE, FALSE, TEXT("DBWIN_DATA_READY"));
  if (DATA_READY == NULL) {
    return ResourcesInitializationError;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return EventAlreadyExistsError;
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
