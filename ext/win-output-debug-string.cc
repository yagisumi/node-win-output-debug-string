#define NAPI_VERSION 4
#include <napi.h>
using namespace Napi;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <cstdio>



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

struct MonitorContext {
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
};


static struct MonitorContext context;

bool initializeSecurity() {
  context.secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  context.secAttr.bInheritHandle = TRUE;
  context.secAttr.lpSecurityDescriptor = &context.secDesc;

  if (!InitializeSecurityDescriptor(&context.secDesc, SECURITY_DESCRIPTOR_REVISION))
    return false;

  if (!SetSecurityDescriptorDacl(&context.secDesc, TRUE, (PACL)NULL, FALSE))
    return false;

  return true;
}

Value ok(const CallbackInfo &info) {
  auto env = info.Env();
  auto obj = Object::New(env);
  obj["ok"] = Boolean::New(env, true);

  return obj;
}

Value error(const CallbackInfo &info, ErrorCode errCode) {
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


ErrorCode initializeResources() {
  context.file = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*context.buf), TEXT("DBWIN_BUFFER"));
  if (context.file == INVALID_HANDLE_VALUE) {
    return ResourcesInitializationError;
  }

  context.buf = static_cast<OdsBuffer *>(MapViewOfFile(context.file, SECTION_MAP_READ, 0, 0, 0));
  if (context.buf == nullptr) {
    return ResourcesInitializationError;
  }

  context.BUFFER_READY = CreateEvent(&context.secAttr, FALSE, FALSE, TEXT("DBWIN_BUFFER_READY"));
  if (context.BUFFER_READY == NULL) {
    return ResourcesInitializationError;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return EventAlreadyExistsError;
  }

  context.DATA_READY = CreateEvent(&context.secAttr, FALSE, FALSE, TEXT("DBWIN_DATA_READY"));
  if (context.DATA_READY == NULL) {
    return ResourcesInitializationError;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return EventAlreadyExistsError;
  }

  return OK;
}

void destroyResources() {
  if (context.DATA_READY != NULL) {
    CloseHandle(context.DATA_READY);
    context.DATA_READY = NULL;
  }

  if (context.BUFFER_READY != NULL) {
    CloseHandle(context.BUFFER_READY);
    context.BUFFER_READY = NULL;
  }

  if (context.buf != nullptr) {
    UnmapViewOfFile(context.buf);
    context.buf = nullptr;
  }

  if (context.file != INVALID_HANDLE_VALUE && context.file != 0) {
    CloseHandle(context.file);
    context.file = 0;
  }
}

Value start(const CallbackInfo &info) {
  if (context.running) {
    return error(info, AlreadyStartingError);
  }

  if (info.Length() == 0 || !info[0].IsFunction()) {
    return error(info, ArgumentError);
  }
  Function func = info[0].As<Function>();

  if (!context.securityInitialized) {
    context.securityInitialized = initializeSecurity();
    if (!context.securityInitialized) {
      return error(info, SecurityInitializationError);
    }
  }

  ErrorCode err = initializeResources();
  if (err != OK) {
    destroyResources();
    return error(info, err);
  }

  context.tsfn = ThreadSafeFunction::New(
    info.Env(),
    func,
    "monitor",
    0,
    1,
    [](Napi::Env) {
      context.nativeThread.join();
    }
  );

  context.running = true;
  context.nativeThread = std::thread([]() {
    auto callback = []( Napi::Env env, Function jsCallback, OdsInfo* info) {
      auto obj = Object::New(env);
      obj["pid"] = Number::New(env, info->pid);
      obj["message"] = String::New(env, reinterpret_cast<const char16_t*>(info->message));
      delete info;

      jsCallback.Call({ obj });
    };

    DWORD r;
    SetEvent(context.BUFFER_READY);

    while (context.running) {
      r = WaitForSingleObject(context.DATA_READY, INFINITE);
      if (r != WAIT_OBJECT_0) {
        break;
      }
      if (!context.running) {
        break;
      }

      OdsInfo* info = new OdsInfo(context.buf);
      napi_status status = context.tsfn.NonBlockingCall(info, callback);
      if (status != napi_ok) {
        delete info;
      }
      SetEvent(context.BUFFER_READY);
    }
    context.tsfn.Release();
    destroyResources();
    context.running = false;
  });

  return ok(info);
}

Value stop(const CallbackInfo &info) {
  context.running = false;
  if (context.DATA_READY) {
    SetEvent(context.DATA_READY);
    return Boolean::New(info.Env(), true);
  }
  return Boolean::New(info.Env(), false);
}

Value outputDebugString(const CallbackInfo& info) {
  if (info.Length() == 1 && info[0].IsString()) {
    auto str = info[0].As<String>().Utf16Value();
    OutputDebugStringW(reinterpret_cast<const wchar_t *>(str.c_str()));
  }

  return info.Env().Undefined();
}

#else
  Value start(const CallbackInfo& info) {
    return info.Env().Undefined();
  }

  Value stop(const CallbackInfo& info) {
    return info.Env().Undefined();
  }

  Value outputDebugString(const CallbackInfo& info) {
    return info.Env().Undefined();
  }
#endif

Object Init(Env env, Object exports) {
  exports["OutputDebugString"] = Function::New(env, outputDebugString);

  auto monitor = Object::New(env);
  monitor["start"] = Function::New(env, start);
  monitor["stop"] = Function::New(env, stop);
  exports["monitor"] = monitor;
  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)
