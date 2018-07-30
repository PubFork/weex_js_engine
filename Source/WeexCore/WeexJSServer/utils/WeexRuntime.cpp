//
// Created by Darin on 28/04/2018.
//

#include "WeexRuntime.h"
#include "WeexCore/WeexJSServer/bridge/script/script_bridge_in_multi_so.h"
#include "WeexCore/WeexJSServer/bridge/script/core_side_in_multi_process.h"
#include "WeexCore/WeexJSServer/object/WeexEnv.h"

using namespace JSC;
using namespace WTF;
using namespace WEEXICU;

WeexRuntime::WeexRuntime(bool isMultiProgress) : is_multi_process_(isMultiProgress), script_bridge_(nullptr) {
    weexObjectHolder.reset(new WeexObjectHolder(isMultiProgress));
    LOGE("WeexRuntime is running and mode is %s", isMultiProgress ? "multiProcess" : "singleProcess");
}

WeexRuntime::WeexRuntime(WeexCore::ScriptBridge *script_bridge, bool isMultiProgress) : WeexRuntime(isMultiProgress) {
    this->script_bridge_ = script_bridge;
}

int WeexRuntime::initFramework(IPCArguments *arguments) {
    base::debug::TraceEvent::StartATrace(nullptr);

    base::debug::TraceScope traceScope("weex", "initFramework");
    weexObjectHolder->initFromIPCArguments(arguments, 1, false);
    const IPCString *ipcSource = arguments->getString(0);
    const String &source = jString2String(ipcSource->content, ipcSource->length);

    return _initFramework(source);
}

int WeexRuntime::initFramework(const String &script, std::vector<INIT_FRAMEWORK_PARAMS *> params) {
    base::debug::TraceEvent::StartATrace(nullptr);
    base::debug::TraceScope traceScope("weex", "initFramework");
    weexObjectHolder->initFromParams(params, false);
    return _initFramework(script);
}

int WeexRuntime::initAppFrameworkMultiProcess(const String &instanceId, const String &appFramework,
                                              IPCArguments *arguments) {
    auto k = instanceId.utf8().data();
    auto pHolder = getLightAppObjectHolder(instanceId);
    if (pHolder == nullptr) {
        auto holder = new WeexObjectHolder(true);
        holder->initFromIPCArguments(arguments, 2, true);
        weexLiteAppObjectHolderMap[k] = holder;
    }
//    LOGE("initAppFrameworkMultiProcess is running and id is %s", k);
    return _initAppFramework(instanceId, appFramework);
    // LOGE("Weex jsserver IPCJSMsg::INITAPPFRAMEWORK end");
}

int WeexRuntime::initAppFramework(const String &instanceId, const String &appFramework,
                                  std::vector<INIT_FRAMEWORK_PARAMS *> params) {
    auto k = instanceId.utf8().data();
    auto pHolder = getLightAppObjectHolder(instanceId);
    if (pHolder == nullptr) {
        auto holder = new WeexObjectHolder(is_multi_process_);
        holder->initFromParams(params, true);
        weexLiteAppObjectHolderMap[k] = holder;
    }
    return _initAppFramework(instanceId, appFramework);
}

int WeexRuntime::createAppContext(const String &instanceId, const String &jsBundle) {
    if (instanceId == "") {
        return static_cast<int32_t>(false);
    } else {
        // new a global object
        // --------------------------------------------------

        auto weexLiteAppObjectHolder = getLightAppObjectHolder(instanceId);
        if (weexLiteAppObjectHolder == nullptr) {
            return static_cast<int32_t>(false);
        }
        std::map<std::string, WeexGlobalObject *>::iterator it_find;
        auto objectMap = weexLiteAppObjectHolder->m_jsAppGlobalObjectMap;
        it_find = objectMap.find(instanceId.utf8().data());
        if (it_find == objectMap.end()) {
            return static_cast<int32_t>(false);
        }
        JSGlobalObject *globalObject_local = objectMap[instanceId.utf8().data()];
        if (globalObject_local == nullptr) {
            return static_cast<int32_t>(false);
        }

        VM &vm_global = *weexObjectHolder->m_globalVM.get();
        JSLockHolder locker_global(&vm_global);

        WeexGlobalObject *globalObject = weexLiteAppObjectHolder->cloneWeexObject(true, true);
        weex::GlobalObjectDelegate *delegate = NULL;
        globalObject->SetScriptBridge(script_bridge_);

        VM &vm = globalObject_local->vm();
        JSLockHolder locker_1(&vm);

        VM &thisVm = globalObject->vm();
        JSLockHolder locker_2(&thisVm);

        // LOGE("Weex jsserver IPCJSMsg::CREATEAPPCONTEXT start00");
        // --------------------------------------------------
        // LOGE("start call __get_app_context_");
        PropertyName createInstanceContextProperty(Identifier::fromString(&vm, "__get_app_context__"));
        ExecState *state = globalObject_local->globalExec();
        // LOGE("start call __get_app_context_ 11");
        JSValue createInstanceContextFunction = globalObject_local->get(state, createInstanceContextProperty);
        // LOGE("start call __get_app_context_ 22");
        MarkedArgumentBuffer args;
        // add __get_app_context_ aras
        // args.append(String2JSValue(state, instanceId));
        // JSValue optsObject = parseToObject(state, opts);
        // args.append(optsObject);
        // JSValue initDataObject = parseToObject(state, initData);
        // args.append(initDataObject);
        CallData callData;
        CallType callType = getCallData(createInstanceContextFunction, callData);
        NakedPtr<Exception> returnedException;
        // LOGE("start call __get_app_context_ ");
        JSValue ret = call(state, createInstanceContextFunction, callType, callData,
                           globalObject_local, args, returnedException);
        // LOGE("end call __get_app_context_");
        if (returnedException) {
            String exceptionInfo = exceptionToString(globalObject_local, returnedException->value());
            // LOGE("getJSGlobalObject returnedException %s", exceptionInfo.utf8().data());
            return static_cast<int32_t>(false);
        }
        globalObject->resetPrototype(vm, ret);
        globalObject->id = instanceId.utf8().data();
        // --------------------------------------------------

        weexLiteAppObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()] = globalObject;
//        VM& vm0 = globalObject->vm();
//        JSLockHolder locker1(&vm0);
        if (!ExecuteJavaScript(globalObject, jsBundle, ("weex createAppContext"), true, "createAppContext",
                               instanceId.utf8().data())) {
            // LOGE("createAppContext and ExecuteJavaScript Error");
            return static_cast<int32_t>(false);
        }
    }
    return static_cast<int32_t>(true);
}

char *WeexRuntime::exeJSOnAppWithResult(const String &instanceId, const String &jsBundle) {
    if (instanceId == "") {
        return nullptr;
    } else {
        auto objectMap = weexObjectHolder->m_jsInstanceGlobalObjectMap;
        JSGlobalObject *globalObject = objectMap[instanceId.utf8().data()];
        if (globalObject == nullptr) {
            return nullptr;
        }

        VM &vm_global = *weexObjectHolder->m_globalVM.get();
        JSLockHolder locker_global(&vm_global);
        VM &vm = globalObject->vm();
        JSLockHolder locker(&vm);

        SourceOrigin sourceOrigin(String::fromUTF8("(weex)"));
        NakedPtr<Exception> evaluationException;
        JSValue returnValue = evaluate(globalObject->globalExec(),
                                       makeSource(jsBundle, sourceOrigin, "execjs on App context"),
                                       JSValue(), evaluationException);
        if (evaluationException) {
            // String exceptionInfo = exceptionToString(globalObject, evaluationException.get()->value());
            // LOGE("EXECJSONINSTANCE exception:%s", exceptionInfo.utf8().data());
            ReportException(globalObject, evaluationException.get(), instanceId.utf8().data(), "execJSOnInstance");
            return nullptr;
        }
        globalObject->vm().drainMicrotasks();
        // LOGE("Weex jsserver IPCJSMsg::EXECJSONAPPWITHRESULT end");
        // WTF::String str = returnValue.toWTFString(globalObject->globalExec());
        const char *data = returnValue.toWTFString(globalObject->globalExec()).utf8().data();
        char *buf = new char[strlen(data) + 1];
        strcpy(buf, data);
        return buf;
    }
}

int
WeexRuntime::callJSOnAppContext(const String &instanceId, const String &func, std::vector<VALUE_WITH_TYPE *> params) {
    // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT instanceId:%s, func:%s", instanceId.utf8().data(), func.utf8().data());
    if (instanceId == "") {
        return static_cast<int32_t>(false);
    } else {
        auto weexLiteAppObjectHolder = getLightAppObjectHolder(instanceId);
        if (weexLiteAppObjectHolder == nullptr) {
            return static_cast<int32_t>(false);
        }

        std::map<std::string, WeexGlobalObject *>::iterator it_find;
        auto objectMap = weexLiteAppObjectHolder->m_jsAppGlobalObjectMap;
        it_find = objectMap.find(instanceId.utf8().data());
        if (it_find == objectMap.end()) {
            // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT mAppGlobalObjectMap donot contain globalObject");
            return static_cast<int32_t>(false);
        }
        JSGlobalObject *globalObject = objectMap[instanceId.utf8().data()];
        if (globalObject == NULL) {
            // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT globalObject is null");
            return static_cast<int32_t>(false);
        }
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT1");
        VM &vm_global = *weexObjectHolder->m_globalVM.get();
        JSLockHolder locker_global(&vm_global);

        VM &vm = globalObject->vm();
        JSLockHolder locker(&vm);
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT2");
        MarkedArgumentBuffer obj;
        ExecState *state = globalObject->globalExec();
        _getArgListFromJSParams(&obj, state, params);
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT3");
        Identifier funcIdentifier = Identifier::fromString(&vm, func);

        JSValue function;
        JSValue result;
        function = globalObject->get(state, funcIdentifier);
        CallData callData;
        CallType callType = getCallData(function, callData);
        NakedPtr<Exception> returnedException;
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT start call js runtime funtion");
        if (function.isEmpty()) {
            LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT js funtion is empty");
        }
        JSValue ret = call(state, function, callType, callData, globalObject, obj, returnedException);
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT end");
        if (returnedException) {
            ReportException(globalObject, returnedException.get(), instanceId.utf8().data(), func.utf8().data());
            return static_cast<int32_t>(false);
        }
        globalObject->vm().drainMicrotasks();
        return static_cast<int32_t>(true);
    }
}

int WeexRuntime::callJSOnAppContext(IPCArguments *arguments) {
    const IPCString *ipcInstanceId = arguments->getString(0);
    const IPCString *ipcFunc = arguments->getString(1);
    String instanceId = jString2String(ipcInstanceId->content, ipcInstanceId->length);
    String func = jString2String(ipcFunc->content, ipcFunc->length);
    // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT instanceId:%s, func:%s", instanceId.utf8().data(), func.utf8().data());

    if (instanceId == "") {
        return static_cast<int32_t>(false);
    } else {
        std::map<std::string, WeexGlobalObject *>::iterator it_find;
        auto weexLiteAppObjectHolder = getLightAppObjectHolder(instanceId);
        if (weexLiteAppObjectHolder == nullptr) {
//            LOGE("callJSOnAppContext is running and id is %s and weexLiteAppObjectHolder is null", instanceId.utf8().data());
            return static_cast<int32_t>(false);
        }
        auto objectMap = weexLiteAppObjectHolder->m_jsAppGlobalObjectMap;
        it_find = objectMap.find(instanceId.utf8().data());
        if (it_find == objectMap.end()) {
//            LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT mAppGlobalObjectMap donot contain globalObject");
            return static_cast<int32_t>(false);
        }
        JSGlobalObject *globalObject = objectMap[instanceId.utf8().data()];
        if (globalObject == NULL) {
//            LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT globalObject is null");
            return static_cast<int32_t>(false);
        }
//        LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT1");
        VM &vm_global = *weexObjectHolder->m_globalVM.get();
        JSLockHolder locker_global(&vm_global);

        VM &vm = globalObject->vm();
        JSLockHolder locker(&vm);
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT2");
        MarkedArgumentBuffer obj;
        ExecState *state = globalObject->globalExec();
        _getArgListFromIPCArguments(&obj, state, arguments, 2);
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT3");
        Identifier funcIdentifier = Identifier::fromString(&vm, func);

        JSValue function;
        JSValue result;
        function = globalObject->get(state, funcIdentifier);
        CallData callData;
        CallType callType = getCallData(function, callData);
        NakedPtr<Exception> returnedException;
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT start call js runtime funtion");
        if (function.isEmpty()) {
            LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT js funtion is empty");
        }
        JSValue ret = call(state, function, callType, callData, globalObject, obj, returnedException);
        // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT end");
        if (returnedException) {
            ReportException(globalObject, returnedException.get(), instanceId.utf8().data(), func.utf8().data());
            return static_cast<int32_t>(false);
        }
        globalObject->vm().drainMicrotasks();
        return static_cast<int32_t>(true);
    }
}

int WeexRuntime::destroyAppContext(const String &instanceId) {
    auto weexLiteAppObjectHolder = getLightAppObjectHolder(instanceId);
    if (weexLiteAppObjectHolder == nullptr) {
        return static_cast<int32_t>(false);
    }
    auto objectMap = weexLiteAppObjectHolder->m_jsAppGlobalObjectMap;
    std::map<std::string, WeexGlobalObject *>::iterator it_find;
    auto k = instanceId.utf8().data();
    it_find = objectMap.find(k);
    if (it_find != objectMap.end()) {
        // LOGE("Weex jsserver IPCJSMsg::DESTORYAPPCONTEXT mAppGlobalObjectMap donnot contain and return");
        objectMap.erase(k);
    }


    // LOGE("Weex jsserver IPCJSMsg::DESTORYAPPCONTEXT end1");
    std::map<std::string, WeexGlobalObject *>::iterator it_find_instance;
    objectMap = weexLiteAppObjectHolder->m_jsInstanceGlobalObjectMap;
    it_find = objectMap.find(k);
    if (it_find_instance != objectMap.end()) {
        // LOGE("Weex jsserver IPCJSMsg::DESTORYAPPCONTEXT mAppInstanceGlobalObjectMap donnot contain and return");
        objectMap.erase(k);
    }

    // GC on VM
//    WeexGlobalObject* instanceGlobalObject = mAppInstanceGlobalObjectMap[instanceId.utf8().data()];
//    if (instanceGlobalObject == NULL) {
//      return static_cast<int32_t>(true);
//    }
//    LOGE("Weex jsserver IPCJSMsg::DESTORYAPPCONTEXT start GC");
//    VM& vm_global = *globalVM.get();
//    JSLockHolder locker_global(&vm_global);
//
//    ExecState* exec_ = instanceGlobalObject->globalExec();
//    JSLockHolder locker_(exec_);
//    VM& vm_ = exec_->vm();
//    vm_.heap.collectAllGarbage();
//    instanceGlobalObject->m_server = nullptr;
//    instanceGlobalObject = NULL;

    weexLiteAppObjectHolderMap.erase(k);
    delete weexLiteAppObjectHolder;
    weexLiteAppObjectHolder = nullptr;
    // LOGE("mAppInstanceGlobalObjectMap size:%d mAppGlobalObjectMap size:%d",
    //      mAppInstanceGlobalObjectMap.size(), mAppGlobalObjectMap.size());
    return static_cast<int32_t>(true);
}

int WeexRuntime::exeJsService(const String &source) {
    base::debug::TraceScope traceScope("weex", "exeJSService");
//    if (weexLiteAppObjectHolder.get() != nullptr) {
//        VM & vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//        JSLockHolder locker_global(&vm_global);
//    }
    JSGlobalObject *globalObject = weexObjectHolder->m_globalObject.get();
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);
    if (!ExecuteJavaScript(globalObject, source, ("weex service"), true, "execjsservice")) {
        LOGE("jsLog JNI_Error >>> scriptStr :%s", source.utf8().data());
        return static_cast<int32_t>(false);
    }
    return static_cast<int32_t>(true);
}

int WeexRuntime::exeCTimeCallback(const String &source) {
    base::debug::TraceScope traceScope("weex", "EXECTIMERCALLBACK");
    LOGE("IPC EXECTIMERCALLBACK and ExecuteJavaScript");
    JSGlobalObject *globalObject = weexObjectHolder->m_globalObject.get();
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);
    if (!ExecuteJavaScript(globalObject, source, ("weex service"), false, "timercallback")) {
        LOGE("jsLog EXECTIMERCALLBACK >>> scriptStr :%s", source.utf8().data());
        return static_cast<int32_t>(false);
    }

    return static_cast<int32_t>(true);
}

int WeexRuntime::exeJS(const String &instanceId, const String &nameSpace, const String &func,
                       std::vector<VALUE_WITH_TYPE *> params) {
    // LOGE("EXECJS func:%s", func.utf8().data());

    String runFunc = func;

    JSGlobalObject *globalObject;
    // fix instanceof Object error
    // if function is callJs on instance, should us Instance object to call __WEEX_CALL_JAVASCRIPT__
    if (std::strcmp("callJS", runFunc.utf8().data()) == 0) {
        globalObject = weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()];
        if (globalObject == NULL) {
            globalObject = weexObjectHolder->m_globalObject.get();
        } else {
            WTF::String funcWString("__WEEX_CALL_JAVASCRIPT__");
            runFunc = funcWString;
        }
    } else {
        globalObject = weexObjectHolder->m_globalObject.get();
    }
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);
//    if (weexLiteAppObjectHolder.get() != nullptr) {
//        VM & vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//        JSLockHolder locker_global(&vm_global);
//    }

    MarkedArgumentBuffer obj;
    base::debug::TraceScope traceScope("weex", "exeJS", "function", runFunc.utf8().data());
    ExecState *state = globalObject->globalExec();
    _getArgListFromJSParams(&obj, state, params);

    Identifier funcIdentifier = Identifier::fromString(&vm, runFunc);

    JSValue function;
    JSValue result;
    if (nameSpace.isEmpty()) {
        function = globalObject->get(state, funcIdentifier);
    } else {
        Identifier namespaceIdentifier = Identifier::fromString(&vm, nameSpace);
        JSValue master = globalObject->get(state, namespaceIdentifier);
        if (!master.isObject()) {
            return static_cast<int32_t>(false);
        }
        function = master.toObject(state)->get(state, funcIdentifier);
    }
    CallData callData;
    CallType callType = getCallData(function, callData);
    NakedPtr<Exception> returnedException;
    JSValue ret = call(state, function, callType, callData, globalObject, obj, returnedException);

    globalObject->vm().drainMicrotasks();


    if (returnedException) {
        ReportException(globalObject, returnedException.get(), instanceId.utf8().data(), func.utf8().data());
        return static_cast<int32_t>(false);
    }
    return static_cast<int32_t>(true);
}

int WeexRuntime::exeJS(const String &instanceId, const String &nameSpace, const String &func, IPCArguments *arguments) {
    // LOGE("EXECJS func:%s", func.utf8().data());

    String runFunc = func;

    JSGlobalObject *globalObject;
    // fix instanceof Object error
    // if function is callJs on instance, should us Instance object to call __WEEX_CALL_JAVASCRIPT__
    if (std::strcmp("callJS", runFunc.utf8().data()) == 0) {
        globalObject = weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()];
        if (globalObject == NULL) {
            globalObject = weexObjectHolder->m_globalObject.get();
        } else {
            WTF::String funcWString("__WEEX_CALL_JAVASCRIPT__");
            runFunc = funcWString;
        }
    } else {
        globalObject = weexObjectHolder->m_globalObject.get();
    }
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);
//    if (weexLiteAppObjectHolder.get() != nullptr) {
//        VM & vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//        JSLockHolder locker_global(&vm_global);
//    }

    MarkedArgumentBuffer obj;
    base::debug::TraceScope traceScope("weex", "exeJS", "function", runFunc.utf8().data());
    ExecState *state = globalObject->globalExec();
    _getArgListFromIPCArguments(&obj, state, arguments, 3);

    Identifier funcIdentifier = Identifier::fromString(&vm, runFunc);

    JSValue function;
    JSValue result;
    if (nameSpace.isEmpty()) {
        function = globalObject->get(state, funcIdentifier);
    } else {
        Identifier namespaceIdentifier = Identifier::fromString(&vm, nameSpace);
        JSValue master = globalObject->get(state, namespaceIdentifier);
        if (!master.isObject()) {
            return static_cast<int32_t>(false);
        }
        function = master.toObject(state)->get(state, funcIdentifier);
    }
    CallData callData;
    CallType callType = getCallData(function, callData);
    NakedPtr<Exception> returnedException;
    JSValue ret = call(state, function, callType, callData, globalObject, obj, returnedException);

    globalObject->vm().drainMicrotasks();


    if (returnedException) {
        ReportException(globalObject, returnedException.get(), instanceId.utf8().data(), func.utf8().data());
        return static_cast<int32_t>(false);
    }
    return static_cast<int32_t>(true);
}

inline void convertJSArrayToWeexJSResult(ExecState *state, JSValue &ret, WeexJSResult &jsResult) {
    if (ret.isUndefined() || ret.isNull() || !isJSArray(ret)) {
        // createInstance return whole source object, which is big, only accept array result
        return;
    }
    //
    /** most scene, return result is array of null */
    JSArray *array = asArray(ret);
    uint32_t length = array->length();
    bool isAllNull = true;
    for (uint32_t i = 0; i < length; i++) {
        JSValue ele = array->getIndex(state, i);
        if (!ele.isUndefinedOrNull()) {
            isAllNull = false;
            break;
        }
    }
    if (isAllNull) {
        return;
    }
    if (WeexEnv::getEnv()->useWson()) {
        wson_buffer *buffer = wson::toWson(state, ret);
        jsResult.data = (char *) buffer->data;
        jsResult.length = buffer->position;
        jsResult.fromMalloc = true;
        buffer->data = nullptr;
        wson_buffer_free(buffer);
    } else {
        String string = JSONStringify(state, ret, 0);
        CString cstring = string.utf8();
        char *buf = new char[cstring.length() + 1];
        memcpy(buf, cstring.data(), cstring.length());
        buf[cstring.length()] = '\0';
        jsResult.data = buf;
        jsResult.length = cstring.length();
        jsResult.fromNew = true;
    }
}

WeexJSResult WeexRuntime::exeJSWithResult(const String &instanceId, const String &nameSpace, const String &func,
                                          std::vector<VALUE_WITH_TYPE *> params) {
    WeexJSResult jsResult;
    JSGlobalObject *globalObject;
    String runFunc = func;
    // fix instanceof Object error
    // if function is callJs should us Instance object to call __WEEX_CALL_JAVASCRIPT__
    if (std::strcmp("callJS", runFunc.utf8().data()) == 0) {
        globalObject = weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()];
        if (globalObject == NULL) {
            globalObject = weexObjectHolder->m_globalObject.get();
        } else {
            WTF::String funcWString("__WEEX_CALL_JAVASCRIPT__");
            runFunc = funcWString;
        }
    } else {
        globalObject = weexObjectHolder->m_globalObject.get();
    }
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);
//    if (weexLiteAppObjectHolder.get() != nullptr) {
//        VM & vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//        JSLockHolder locker_global(&vm_global);
//    }

    base::debug::TraceScope traceScope("weex", "exeJSWithResult", "function", runFunc.utf8().data());

    MarkedArgumentBuffer obj;
    ExecState *state = globalObject->globalExec();

    _getArgListFromJSParams(&obj, state, params);

    Identifier funcIdentifier = Identifier::fromString(&vm, runFunc);
    JSValue function;
    JSValue result;
    if (nameSpace.isEmpty()) {
        function = globalObject->get(state, funcIdentifier);
    } else {
        Identifier namespaceIdentifier = Identifier::fromString(&vm, nameSpace);
        JSValue master = globalObject->get(state, namespaceIdentifier);
        if (!master.isObject()) {
            return jsResult;
        }
        function = master.toObject(state)->get(state, funcIdentifier);
    }
    CallData callData;
    CallType callType = getCallData(function, callData);
    NakedPtr<Exception> returnedException;
    JSValue ret = call(state, function, callType, callData, globalObject, obj, returnedException);
    globalObject->vm().drainMicrotasks();

    if (returnedException) {
        ReportException(globalObject, returnedException.get(), instanceId.utf8().data(), func.utf8().data());
        return jsResult;
    }
    convertJSArrayToWeexJSResult(state, ret, jsResult);
    return jsResult;
}


WeexJSResult WeexRuntime::exeJSWithResult(const String &instanceId, const String &nameSpace, const String &func,
                                          IPCArguments *arguments) {
    WeexJSResult jsResult;

    JSGlobalObject *globalObject;
    String runFunc = func;
    // fix instanceof Object error
    // if function is callJs should us Instance object to call __WEEX_CALL_JAVASCRIPT__
    if (std::strcmp("callJS", runFunc.utf8().data()) == 0) {
        globalObject = weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()];
        if (globalObject == NULL) {
            globalObject = weexObjectHolder->m_globalObject.get();
        } else {
            WTF::String funcWString("__WEEX_CALL_JAVASCRIPT__");
            runFunc = funcWString;
        }
    } else {
        globalObject = weexObjectHolder->m_globalObject.get();
    }
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);

    base::debug::TraceScope traceScope("weex", "exeJSWithResult", "function", runFunc.utf8().data());

    MarkedArgumentBuffer obj;
    ExecState *state = globalObject->globalExec();

    _getArgListFromIPCArguments(&obj, state, arguments, 3);


    Identifier funcIdentifier = Identifier::fromString(&vm, runFunc);
    JSValue function;
    JSValue result;
    if (nameSpace.isEmpty()) {
        function = globalObject->get(state, funcIdentifier);
    } else {
        Identifier namespaceIdentifier = Identifier::fromString(&vm, nameSpace);
        JSValue master = globalObject->get(state, namespaceIdentifier);
        if (!master.isObject()) {
            return jsResult;
        }
        function = master.toObject(state)->get(state, funcIdentifier);
    }
    CallData callData;
    CallType callType = getCallData(function, callData);
    NakedPtr<Exception> returnedException;
    JSValue ret = call(state, function, callType, callData, globalObject, obj, returnedException);
    globalObject->vm().drainMicrotasks();

    if (returnedException) {
        ReportException(globalObject, returnedException.get(), instanceId.utf8().data(), func.utf8().data());
        return jsResult;
    }
    convertJSArrayToWeexJSResult(state, ret, jsResult);
    return jsResult;
}


char *WeexRuntime::exeJSOnInstance(const String &instanceId, const String &script) {
    JSGlobalObject *globalObject = weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()];
    if (globalObject == NULL) {
        globalObject = weexObjectHolder->m_globalObject.get();
    }
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);
//    if (weexLiteAppObjectHolder.get() != nullptr) {
//        VM & vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//        JSLockHolder locker_global(&vm_global);
//    }

    SourceOrigin sourceOrigin(String::fromUTF8("(weex)"));
    NakedPtr<Exception> evaluationException;
    JSValue returnValue = evaluate(globalObject->globalExec(),
                                   makeSource(script, sourceOrigin, "execjs on instance context"), JSValue(),
                                   evaluationException);
    globalObject->vm().drainMicrotasks();
    if (evaluationException) {
        // String exceptionInfo = exceptionToString(globalObject, evaluationException.get()->value());
        // LOGE("EXECJSONINSTANCE exception:%s", exceptionInfo.utf8().data());
        ReportException(globalObject, evaluationException.get(), instanceId.utf8().data(), "execJSOnInstance");
        return nullptr;
    }
    // WTF::String str = returnValue.toWTFString(globalObject->globalExec());
    const char *data = returnValue.toWTFString(globalObject->globalExec()).utf8().data();
    char *buf = new char[strlen(data) + 1];
    strcpy(buf, data);
    return buf;
}

int WeexRuntime::destroyInstance(const String &instanceId) {

    // LOGE("IPCJSMsg::DESTORYINSTANCE");
    auto *globalObject = weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()];
    if (globalObject == NULL) {
        return static_cast<int32_t>(true);
    }
    // LOGE("DestoryInstance map 11 length:%d", weexObjectHolder->m_jsGlobalObjectMap.size());
    weexObjectHolder->m_jsInstanceGlobalObjectMap.erase(instanceId.utf8().data());
    // LOGE("DestoryInstance map 22 length:%d", weexObjectHolder->m_jsGlobalObjectMap.size());

    // release JSGlobalContextRelease
    // when instanceId % 20 == 0 GC
    bool needGc = false;
    if (instanceId.length() > 0) {
        int index = atoi(instanceId.utf8().data());
        if (index > 0 && index % 20 == 0) {
            // LOGE("needGc is true, instanceId.utf8().data():%s index:%d", instanceId.utf8().data(), index);
            needGc = true;
        }
    }
    if (needGc) {
//        if (weexLiteAppObjectHolder.get() != nullptr) {
//            VM & vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//            JSLockHolder locker_global(&vm_global);
//        }
        ExecState *exec = globalObject->globalExec();
        JSLockHolder locker(exec);
        VM &vm = exec->vm();
        vm.heap.collectAllGarbage();
    }

//    globalObject->m_server = nullptr;
    globalObject = NULL;

    return static_cast<int32_t>(true);
}

int WeexRuntime::updateGlobalConfig(const String &config) {
    JSGlobalObject *globalObject = weexObjectHolder->m_globalObject.get();
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);

//    if (weexLiteAppObjectHolder.get() != nullptr) {
//        VM & vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//        JSLockHolder locker_global(&vm_global);
//    }


    const char *configChar = config.utf8().data();
    doUpdateGlobalSwitchConfig(configChar);
    return static_cast<int32_t>(true);
}

int WeexRuntime::createInstance(const String &instanceId, const String &func, const String &script, const String &opts,
                                const String &initData,
                                const String &extendsApi) {
    JSGlobalObject *impl_globalObject = weexObjectHolder->m_globalObject.get();
    JSGlobalObject *globalObject;
    if (instanceId == "") {
        globalObject = impl_globalObject;
    } else {
        auto *temp_object = weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()];

        if (temp_object == NULL) {
            // new a global object
            // --------------------------------------------------
//            if (weexLiteAppObjectHolder.get() != nullptr) {
//                VM &vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//                JSLockHolder locker_global(&vm_global);
//            }

            temp_object = weexObjectHolder->cloneWeexObject(true, false);
            VM &vm = temp_object->vm();
            temp_object->SetScriptBridge(script_bridge_);
            JSLockHolder locker(&vm);

            // --------------------------------------------------

            // use impl global object run createInstanceContext
            // --------------------------------------------------
            // RAx or vue
            JSGlobalObject *globalObject_local = impl_globalObject;
            PropertyName createInstanceContextProperty(Identifier::fromString(&vm, "createInstanceContext"));
            ExecState *state = globalObject_local->globalExec();
            JSValue createInstanceContextFunction = globalObject_local->get(state, createInstanceContextProperty);
            MarkedArgumentBuffer args;
            args.append(String2JSValue(state, instanceId));
            JSValue optsObject = parseToObject(state, opts);
            args.append(optsObject);
            JSValue initDataObject = parseToObject(state, initData);
            args.append(initDataObject);
            // args.append(String2JSValue(state, ""));
            CallData callData;
            CallType callType = getCallData(createInstanceContextFunction, callData);
            NakedPtr<Exception> returnedException;
            JSValue ret = call(state, createInstanceContextFunction, callType, callData,
                               globalObject_local, args, returnedException);
            if (returnedException) {
                // ReportException(globalObject, returnedException.get(), nullptr, "");
                String exceptionInfo = exceptionToString(globalObject_local, returnedException->value());
                LOGE("getJSGlobalObject returnedException %s", exceptionInfo.utf8().data());
            }
            // --------------------------------------------------

            // String str = getArgumentAsString(state, ret);
            //(ret.toWTFString(state));

            // use it to set Vue prototype to instance context
            JSObject *object = ret.toObject(state, temp_object);
            JSObjectRef ref = toRef(object);
            JSGlobalContextRef contextRef = toGlobalRef(state);
            JSValueRef vueRef = JSObjectGetProperty(contextRef, ref, JSStringCreateWithUTF8CString("Vue"), nullptr);
            if (vueRef != nullptr) {
                JSObjectRef vueObject = JSValueToObject(contextRef, vueRef, nullptr);
                if (vueObject != nullptr) {
                    JSGlobalContextRef instanceContextRef = toGlobalRef(temp_object->globalExec());
                    JSObjectSetPrototype(instanceContextRef, vueObject,
                                         JSObjectGetPrototype(instanceContextRef,
                                                              JSContextGetGlobalObject(instanceContextRef)));
                }
            }
            //-------------------------------------------------

            temp_object->resetPrototype(vm, ret);
            weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()] = temp_object;

            // -----------------------------------------
            // ExecState* exec =temp_object->globalExec();
            // JSLockHolder temp_locker(exec);
            // VM& temp_vm = exec->vm();
            // gcProtect(exec->vmEntryGlobalObject());
            // temp_vm.ref();
            // ------------------------------------------
        }
        globalObject = temp_object;
    }

//    if (weexLiteAppObjectHolder.get() != nullptr) {
//        VM &vm_global = *weexLiteAppObjectHolder->m_globalVM.get();
//        JSLockHolder locker_global(&vm_global);
//    }

    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);

    // if extend api is not null should exec befor createInstanceContext, such as rax-api
    if (!extendsApi.isEmpty() && extendsApi.length() > 0) {
        if (!ExecuteJavaScript(globalObject, extendsApi, ("weex run raxApi"), true,
                               "runRaxApi", instanceId.utf8().data())) {
            LOGE("before createInstanceContext run rax api Error");
            return static_cast<int32_t>(false);
        }
    }
    if (!ExecuteJavaScript(globalObject, script, ("weex createInstanceContext"), true,
                           "createInstanceContext", instanceId.utf8().data())) {
        LOGE("createInstanceContext and ExecuteJavaScript Error");
        return static_cast<int32_t>(false);
    }
    return static_cast<int32_t>(true);
}

int WeexRuntime::_initFramework(const String &source) {
    VM &vm = *weexObjectHolder->m_globalVM.get();
    JSLockHolder locker(&vm);

    auto globalObject = weexObjectHolder->m_globalObject.get();
    globalObject->SetScriptBridge(script_bridge_);

    if (!ExecuteJavaScript(globalObject, source, "(weex framework)", true, "initFramework", "")) {
        return false;
    }

    setJSFVersion(globalObject);
    return true;
}

int WeexRuntime::_initAppFramework(const String &instanceId, const String &appFramework) {
    VM &vm = *weexObjectHolder->m_globalVM.get();
    JSLockHolder locker_global(&vm);

    auto weexLiteAppObjectHolder = getLightAppObjectHolder(instanceId);
    if (weexLiteAppObjectHolder == nullptr) {
        return static_cast<int32_t>(false);
    }

    auto globalObject = weexLiteAppObjectHolder->m_globalObject.get();
    globalObject->SetScriptBridge(script_bridge_);

    const char *id = instanceId.utf8().data();
    globalObject->id = id;
    // LOGE("Weex jsserver IPCJSMsg::INITAPPFRAMEWORK 1");
    weexLiteAppObjectHolder->m_jsAppGlobalObjectMap[id] = globalObject;
    weexLiteAppObjectHolder->m_jsInstanceGlobalObjectMap[id] = globalObject;
    // LOGE("Weex jsserver IPCJSMsg::INITAPPFRAMEWORK 2");
    return static_cast<int32_t>(
            ExecuteJavaScript(globalObject,
                              appFramework,
                              "(app framework)",
                              true,
                              "initAppFramework",
                              id));
}

void
WeexRuntime::_getArgListFromIPCArguments(MarkedArgumentBuffer *obj, ExecState *state, IPCArguments *arguments,
                                         size_t start) {
    size_t count = arguments->getCount();
    for (size_t i = start; i < count; ++i) {
        switch (arguments->getType(i)) {
            case IPCType::DOUBLE:
                obj->append(jsNumber(arguments->get<double>(i)));
                break;
            case IPCType::STRING: {
                const IPCString *ipcstr = arguments->getString(i);
                obj->append(jString2JSValue(state, ipcstr->content, ipcstr->length));
            }
                break;
            case IPCType::JSONSTRING: {
                const IPCString *ipcstr = arguments->getString(i);

                String str = jString2String(ipcstr->content, ipcstr->length);

                JSValue o = parseToObject(state, str);
                obj->append(o);
            }
                break;
            case IPCType::BYTEARRAY: {
                const IPCByteArray *array = arguments->getByteArray(i);
                JSValue o = wson::toJSValue(state, (void *) array->content, array->length);
                obj->append(o);
            }
                break;
            default:
                obj->append(jsUndefined());
                break;
        }
    }
}

void WeexRuntime::_getArgListFromJSParams(MarkedArgumentBuffer *obj, ExecState *state,
                                          std::vector<VALUE_WITH_TYPE *> params) {
    for (unsigned int i = 0; i < params.size(); i++) {
        VALUE_WITH_TYPE *paramsObject = params[i];
        switch (paramsObject->type) {
            case ParamsType::DOUBLE:
                obj->append(jsNumber(paramsObject->value.doubleValue));
                break;
            case ParamsType::STRING: {
                WeexString *ipcstr = paramsObject->value.string;
                obj->append(jString2JSValue(state, ipcstr->content, ipcstr->length));
            }
                break;
            case ParamsType::JSONSTRING: {

                const WeexString *ipcstr = paramsObject->value.string;

                String str = jString2String(ipcstr->content, ipcstr->length);
                JSValue o = parseToObject(state, str);
                obj->append(o);
            }
                break;
            case ParamsType::BYTEARRAY: {
                const WeexByteArray *array = paramsObject->value.byteArray;
                JSValue o = wson::toJSValue(state, (void *) array->content, array->length);
                obj->append(o);
            }
                break;
            default:
                obj->append(jsUndefined());
                break;
        }
    }
}

WeexObjectHolder *WeexRuntime::getLightAppObjectHolder(const String &instanceId) {
    auto k = instanceId.utf8().data();
    auto iterator = weexLiteAppObjectHolderMap.find(k);
    if (iterator == weexLiteAppObjectHolderMap.end()) {
        return nullptr;
    }

    return weexLiteAppObjectHolderMap[k];
}

int WeexRuntime::exeTimerFunction(const String &instanceId, JSC::JSValue timerFunction) {
    LOGE("exeTimerFunction is running");
    uint64_t begin = microTime();

    JSGlobalObject *globalObject;
    globalObject = weexObjectHolder->m_jsInstanceGlobalObjectMap[instanceId.utf8().data()];
    if (globalObject == NULL) {
        // instance is not exist
        globalObject = weexObjectHolder->m_globalObject.get();
    }
    VM &vm = globalObject->vm();
    JSLockHolder locker(&vm);
    const JSValue &value = timerFunction;
    JSValue result;
    CallData callData;
    CallType callType = getCallData(value, callData);
    NakedPtr<Exception> returnedException;
    // LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT start call js runtime funtion");
    if (value.isEmpty()) {
        LOGE("Weex jsserver IPCJSMsg::CALLJSONAPPCONTEXT js funtion is empty");
    }

    ArgList a;

    JSValue ret = call(globalObject->globalExec(), value, callType, callData, globalObject, a, returnedException);
    uint64_t end = microTime();

    LOGE("exeTimerFunction cost %lld", end - begin);

    return 0;
}











