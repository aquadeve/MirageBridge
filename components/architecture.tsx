export function Architecture() {
  return (
    <section id="architecture" className="py-20 px-4 sm:px-6 lg:px-8 bg-muted/30">
      <div className="mx-auto max-w-7xl">
        <div className="text-center mb-16">
          <h2 className="text-3xl sm:text-4xl font-bold text-foreground">
            System Architecture
          </h2>
          <p className="mt-4 text-lg text-muted-foreground max-w-2xl mx-auto">
            Understanding how MirageBridge connects your Android device to Termux VR applications.
          </p>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-2 gap-8 items-start">
          {/* Architecture Diagram */}
          <div className="rounded-xl border border-border bg-card p-8">
            <h3 className="text-lg font-semibold text-foreground mb-6">Data Flow</h3>
            <div className="space-y-4">
              {/* Android App Layer */}
              <div className="rounded-lg border border-primary/30 bg-primary/5 p-4">
                <div className="text-sm font-medium text-primary mb-2">Android App (GVR)</div>
                <div className="grid grid-cols-2 gap-2 text-xs text-muted-foreground">
                  <div className="rounded bg-code-bg px-2 py-1">GVR Tracking Adapter</div>
                  <div className="rounded bg-code-bg px-2 py-1">EGL Capture Pipeline</div>
                  <div className="rounded bg-code-bg px-2 py-1">Stereo Renderer</div>
                  <div className="rounded bg-code-bg px-2 py-1">Frame Timing</div>
                </div>
              </div>

              {/* Arrow */}
              <div className="flex justify-center">
                <div className="flex flex-col items-center text-muted-foreground">
                  <div className="h-8 w-px bg-border" />
                  <span className="text-xs my-1">Shared Memory / Unix Socket</span>
                  <div className="h-8 w-px bg-border" />
                </div>
              </div>

              {/* Daemon Layer */}
              <div className="rounded-lg border border-accent/30 bg-accent/5 p-4">
                <div className="text-sm font-medium text-accent mb-2">Termux Daemon</div>
                <div className="grid grid-cols-2 gap-2 text-xs text-muted-foreground">
                  <div className="rounded bg-code-bg px-2 py-1">Transport Reader</div>
                  <div className="rounded bg-code-bg px-2 py-1">Ring Buffer Writer</div>
                  <div className="rounded bg-code-bg px-2 py-1">Pose Client</div>
                  <div className="rounded bg-code-bg px-2 py-1">Frame Client</div>
                </div>
              </div>

              {/* Arrow */}
              <div className="flex justify-center">
                <div className="flex flex-col items-center text-muted-foreground">
                  <div className="h-8 w-px bg-border" />
                  <span className="text-xs my-1">OpenXR Shim API</span>
                  <div className="h-8 w-px bg-border" />
                </div>
              </div>

              {/* App Layer */}
              <div className="rounded-lg border border-border bg-secondary p-4">
                <div className="text-sm font-medium text-foreground mb-2">Your VR Application</div>
                <div className="text-xs text-muted-foreground">
                  Uses standard OpenXR calls to receive tracking and render frames
                </div>
              </div>
            </div>
          </div>

          {/* Protocol Details */}
          <div className="space-y-6">
            <div className="rounded-xl border border-border bg-card p-6">
              <h3 className="text-lg font-semibold text-foreground mb-4">XRPacket Structure</h3>
              <pre className="text-xs text-muted-foreground overflow-x-auto">
                <code>{`struct XRPacket {
  uint32_t magic;         // 0x4D425247
  uint32_t version;       // Protocol version
  uint64_t frameId;       // Frame counter
  uint64_t monotonicNs;   // Timestamp (ns)
  uint64_t predictedDisplayNs;
  double timestampSec;
  float pos[3];           // Head position
  float rot[4];           // Head rotation (quat)
  float angularVel[3];    // Angular velocity
  float linearVel[3];     // Linear velocity
  EyePacket eyes[2];      // Per-eye view/proj
  ControllerPacket controllers[2];
  uint32_t displayWidth;
  uint32_t displayHeight;
  uint32_t displayHz;
};`}</code>
              </pre>
            </div>

            <div className="rounded-xl border border-border bg-card p-6">
              <h3 className="text-lg font-semibold text-foreground mb-4">Frame Specifications</h3>
              <div className="space-y-3">
                <div className="flex justify-between text-sm">
                  <span className="text-muted-foreground">Resolution</span>
                  <span className="text-foreground font-mono">2048 x 1024</span>
                </div>
                <div className="flex justify-between text-sm">
                  <span className="text-muted-foreground">Format</span>
                  <span className="text-foreground font-mono">RGBA (4 bytes/pixel)</span>
                </div>
                <div className="flex justify-between text-sm">
                  <span className="text-muted-foreground">Frame Size</span>
                  <span className="text-foreground font-mono">8 MB per frame</span>
                </div>
                <div className="flex justify-between text-sm">
                  <span className="text-muted-foreground">Ring Buffer Slots</span>
                  <span className="text-foreground font-mono">8 (frames) / 512 (tracking)</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
