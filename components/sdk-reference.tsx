"use client";

import { useState } from "react";
import { Check, Copy } from "lucide-react";
import { cn } from "@/lib/utils";

const tabs = [
  { id: "openxr", label: "OpenXR Shim" },
  { id: "reader", label: "Transport Reader" },
  { id: "protocol", label: "Protocol" },
  { id: "android", label: "Android JNI" },
];

const codeExamples: Record<string, { title: string; description: string; code: string }[]> = {
  openxr: [
    {
      title: "Wait for Frame",
      description: "Wait for the next frame to be ready for rendering.",
      code: `#include "openxr_shim.h"

XrSession session = /* your session */;
XrFrameState frameState;

// Wait for frame timing info
XrResult result = xrWaitFrame(session, nullptr, &frameState);
if (result == XR_SUCCESS) {
    XrTime displayTime = frameState.predictedDisplayTime;
    bool shouldRender = frameState.shouldRender;
}`,
    },
    {
      title: "Locate Views",
      description: "Get the current head pose and view matrices.",
      code: `XrView views[2];
uint32_t viewCount;
XrViewState viewState;

XrResult result = xrLocateViews(
    session,
    nullptr,  // viewLocateInfo
    &viewState,
    2,        // capacity
    &viewCount,
    views
);

// Access view data
for (uint32_t i = 0; i < viewCount; i++) {
    float* position = views[i].posePosition;
    float* orientation = views[i].poseOrientation;
    float* fov = views[i].fov;
}`,
    },
    {
      title: "Frame Lifecycle",
      description: "Complete frame lifecycle with begin and end calls.",
      code: `// Begin frame rendering
xrBeginFrame(session, nullptr);

// Your rendering code here...
// - Access view matrices from xrLocateViews
// - Render left and right eye views
// - Submit to display

// End frame and present
xrEndFrame(session, nullptr);`,
    },
  ],
  reader: [
    {
      title: "Initialize Ring Reader",
      description: "Open a shared memory ring buffer for reading tracking data.",
      code: `#include "transport_reader.h"

using namespace miragebridge;

RingReader reader;
const auto config = DefaultConfig();

// Open the tracking ring buffer
bool success = reader.Open(
    config.trackingName,    // "/miragebridge_tracking"
    sizeof(XRPacket)        // Expected slot size
);

if (!success) {
    // Handle error - daemon may not be running
}`,
    },
    {
      title: "Read Latest Tracking",
      description: "Read the most recent tracking packet from the ring buffer.",
      code: `XRPacket packet;
uint64_t sequence;

// Read latest tracking data (non-blocking)
bool hasData = reader.ReadLatest(&packet, sizeof(packet), &sequence);

if (hasData && packet.magic == kProtocolMagic) {
    // Access head pose
    float* position = packet.pos;
    float* rotation = packet.rot;  // Quaternion
    
    // Access velocities
    float* angularVel = packet.angularVel;
    float* linearVel = packet.linearVel;
    
    // Timestamps
    uint64_t frameId = packet.frameId;
    double timestamp = packet.timestampSec;
}`,
    },
    {
      title: "Read Frame Data",
      description: "Read stereo frame data for rendering.",
      code: `RingReader frameReader;

frameReader.Open(config.frameName, sizeof(SBSFramePacket));

SBSFramePacket frame;
uint64_t seq;

if (frameReader.ReadLatest(&frame, sizeof(frame), &seq)) {
    // Frame header info
    uint32_t width = frame.header.sbsWidth;   // 2048
    uint32_t height = frame.header.sbsHeight; // 1024
    uint64_t frameId = frame.header.frameId;
    
    // Pixel data (RGBA, side-by-side stereo)
    uint8_t* pixels = frame.payload;
    // Left eye: [0, width/2), Right eye: [width/2, width)
}`,
    },
  ],
  protocol: [
    {
      title: "Protocol Constants",
      description: "Key constants defined in the protocol header.",
      code: `namespace miragebridge {

// Magic number for packet validation
constexpr uint32_t kProtocolMagic = 0x4D425247; // "MBRG"
constexpr uint32_t kProtocolVersion = 1;

// Frame dimensions
constexpr uint32_t kSbsWidth = 2048;
constexpr uint32_t kSbsHeight = 1024;
constexpr uint32_t kSbsBytes = kSbsWidth * kSbsHeight * 4;

// Socket chunk types
constexpr uint32_t kSocketChunkMagic = 0x4D425343;
constexpr uint32_t kSocketChunkTracking = 1;
constexpr uint32_t kSocketChunkFrame = 2;

}`,
    },
    {
      title: "Eye Packet Structure",
      description: "Per-eye view and projection matrices.",
      code: `struct EyePacket {
    float view[16];  // 4x4 view matrix (column-major)
    float proj[16];  // 4x4 projection matrix
};

// Access matrices for rendering
void setupEyeMatrices(const EyePacket& eye) {
    // View matrix transforms world -> eye space
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, eye.view);
    
    // Projection matrix for this eye's frustum
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, eye.proj);
}`,
    },
    {
      title: "Controller Packet",
      description: "Controller input state structure.",
      code: `struct ControllerPacket {
    uint32_t id;           // Controller index (0 or 1)
    uint32_t buttons;      // Bitmask of pressed buttons
    float trigger;         // Trigger value [0.0 - 1.0]
    float joystick[2];     // Joystick X, Y [-1.0 - 1.0]
    float position[3];     // World position
    float rotation[4];     // Orientation quaternion
};

// Button bitmask constants (example)
#define BTN_TRIGGER    (1 << 0)
#define BTN_GRIP       (1 << 1)
#define BTN_MENU       (1 << 2)
#define BTN_JOYSTICK   (1 << 3)`,
    },
  ],
  android: [
    {
      title: "Start Bridge",
      description: "Initialize the MirageBridge native core from Android.",
      code: `// In your Activity or Service (Kotlin)
val nativeContext = gvrLayout.gvrApi.nativeGvrContext

// JNI call to start the bridge
external fun nativeStartBridge(
    dataPath: String,
    gvrContext: Long
)

// Start with app data path and GVR context
nativeStartBridge(
    applicationInfo.dataDir,
    nativeContext
)`,
    },
    {
      title: "JNI Bridge Implementation",
      description: "Native C++ JNI entry points.",
      code: `#include <jni.h>
#include "mirage_bridge_core.h"

extern "C" {

JNIEXPORT void JNICALL
Java_com_miragebridge_MirageBridgeService_nativeStartBridge(
    JNIEnv* env,
    jobject /* this */,
    jstring dataPath,
    jlong gvrContext
) {
    const char* path = env->GetStringUTFChars(dataPath, nullptr);
    
    JavaVM* vm;
    env->GetJavaVM(&vm);
    
    miragebridge::StartBridge(vm, path, (void*)gvrContext);
    
    env->ReleaseStringUTFChars(dataPath, path);
}

JNIEXPORT void JNICALL
Java_com_miragebridge_MirageBridgeService_nativeStopBridge(
    JNIEnv* /* env */,
    jobject /* this */
) {
    miragebridge::StopBridge();
}

}`,
    },
    {
      title: "Service Lifecycle",
      description: "Android foreground service for continuous tracking.",
      code: `class MirageBridgeService : Service() {
    
    override fun onStartCommand(
        intent: Intent?,
        flags: Int,
        startId: Int
    ): Int {
        val gvrContext = intent?.getLongExtra(
            "native_gvr_context", 0L
        ) ?: 0L
        
        // Start foreground to prevent system kill
        startForeground(NOTIFICATION_ID, createNotification())
        
        // Initialize native bridge
        nativeStartBridge(
            applicationInfo.dataDir,
            gvrContext
        )
        
        return START_STICKY
    }
    
    override fun onDestroy() {
        nativeStopBridge()
        super.onDestroy()
    }
}`,
    },
  ],
};

function CodeBlock({ code }: { code: string }) {
  const [copied, setCopied] = useState(false);

  const handleCopy = async () => {
    await navigator.clipboard.writeText(code);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="relative group">
      <pre className="rounded-lg border border-code-border bg-code-bg p-4 overflow-x-auto">
        <code className="text-sm text-muted-foreground font-mono whitespace-pre">{code}</code>
      </pre>
      <button
        onClick={handleCopy}
        className="absolute top-2 right-2 p-2 rounded-md bg-secondary opacity-0 group-hover:opacity-100 transition-opacity"
        aria-label="Copy code"
      >
        {copied ? (
          <Check className="h-4 w-4 text-green-500" />
        ) : (
          <Copy className="h-4 w-4 text-muted-foreground" />
        )}
      </button>
    </div>
  );
}

export function SDKReference() {
  const [activeTab, setActiveTab] = useState("openxr");

  return (
    <section id="sdk" className="py-20 px-4 sm:px-6 lg:px-8 bg-muted/30">
      <div className="mx-auto max-w-7xl">
        <div className="text-center mb-16">
          <h2 className="text-3xl sm:text-4xl font-bold text-foreground">
            SDK Reference
          </h2>
          <p className="mt-4 text-lg text-muted-foreground max-w-2xl mx-auto">
            Explore the APIs and learn how to integrate MirageBridge into your VR applications.
          </p>
        </div>

        {/* Tab Navigation */}
        <div className="flex flex-wrap justify-center gap-2 mb-8">
          {tabs.map((tab) => (
            <button
              key={tab.id}
              onClick={() => setActiveTab(tab.id)}
              className={cn(
                "px-4 py-2 rounded-lg text-sm font-medium transition-all",
                activeTab === tab.id
                  ? "bg-primary text-primary-foreground"
                  : "bg-secondary text-muted-foreground hover:text-foreground"
              )}
            >
              {tab.label}
            </button>
          ))}
        </div>

        {/* Code Examples */}
        <div className="space-y-8">
          {codeExamples[activeTab].map((example, index) => (
            <div key={index} className="rounded-xl border border-border bg-card p-6">
              <h3 className="text-lg font-semibold text-foreground mb-2">
                {example.title}
              </h3>
              <p className="text-sm text-muted-foreground mb-4">{example.description}</p>
              <CodeBlock code={example.code} />
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}
