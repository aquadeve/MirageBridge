#pragma once

#include <jni.h>

namespace miragebridge {

void StartBridge(JavaVM* vm, const char* dataPath, void* gvrContext);
void StopBridge();
void OnDisplaySurfaceCreated();
void OnDisplaySurfaceChanged(int width, int height);
void DrawDisplayFrame();

}
