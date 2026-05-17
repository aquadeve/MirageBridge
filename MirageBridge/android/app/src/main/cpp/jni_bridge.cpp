#include "jni_bridge.h"

#include <android/log.h>
#include <jni.h>
#include <string>

#include "core/mirage_bridge_core.h"

#define MB_LOG_TAG "MirageBridgeJNI"
#define MB_LOGI(...) __android_log_print(ANDROID_LOG_INFO, MB_LOG_TAG, __VA_ARGS__)

using namespace miragebridge;

extern "C" JNIEXPORT void JNICALL
Java_com_miragebridge_MirageBridgeService_nativeStart(JNIEnv* env, jobject, jstring dataPath, jlong nativeGvrContext) {
    const char* pathChars = env->GetStringUTFChars(dataPath, nullptr);
    std::string path = pathChars ? pathChars : "";
    if (pathChars) {
        env->ReleaseStringUTFChars(dataPath, pathChars);
    }
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    StartBridge(vm, path.c_str(), reinterpret_cast<void*>(nativeGvrContext));
    MB_LOGI("nativeStart path=%s gvr=%p", path.c_str(), reinterpret_cast<void*>(nativeGvrContext));
}

extern "C" JNIEXPORT void JNICALL
Java_com_miragebridge_MirageBridgeService_nativeStop(JNIEnv*, jobject) {
    StopBridge();
    MB_LOGI("nativeStop");
}

extern "C" JNIEXPORT void JNICALL
Java_com_miragebridge_MainActivity_nativeOnSurfaceCreated(JNIEnv*, jobject) {
    OnDisplaySurfaceCreated();
}

extern "C" JNIEXPORT void JNICALL
Java_com_miragebridge_MainActivity_nativeOnSurfaceChanged(JNIEnv*, jobject, jint width, jint height) {
    OnDisplaySurfaceChanged(width, height);
}

extern "C" JNIEXPORT void JNICALL
Java_com_miragebridge_MainActivity_nativeDrawFrame(JNIEnv*, jobject) {
    DrawDisplayFrame();
}
