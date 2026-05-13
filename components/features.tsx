import { Cpu, Eye, Zap, Layers, Radio, Shield } from "lucide-react";

const features = [
  {
    icon: Eye,
    title: "6DoF Head Tracking",
    description:
      "Full six degrees of freedom head tracking with position, rotation, angular velocity, and linear velocity data at high refresh rates.",
  },
  {
    icon: Layers,
    title: "Stereo Frame Streaming",
    description:
      "Side-by-side stereo frame capture and streaming at 2048x1024 resolution with minimal latency through shared memory transport.",
  },
  {
    icon: Radio,
    title: "Dual Transport Modes",
    description:
      "Choose between high-performance shared memory for local apps or Unix socket transport for cross-process communication.",
  },
  {
    icon: Cpu,
    title: "OpenXR Shim Layer",
    description:
      "Drop-in OpenXR compatibility layer lets existing XR applications work seamlessly with MirageBridge tracking data.",
  },
  {
    icon: Zap,
    title: "Low Latency Pipeline",
    description:
      "Optimized EGL capture pipeline and lock-free ring buffers ensure minimal motion-to-photon latency for smooth VR experiences.",
  },
  {
    icon: Shield,
    title: "Controller Support",
    description:
      "Support for up to 2 controllers with button states, triggers, joysticks, position, and rotation tracking.",
  },
];

export function Features() {
  return (
    <section id="features" className="py-20 px-4 sm:px-6 lg:px-8">
      <div className="mx-auto max-w-7xl">
        <div className="text-center mb-16">
          <h2 className="text-3xl sm:text-4xl font-bold text-foreground">
            Built for Performance
          </h2>
          <p className="mt-4 text-lg text-muted-foreground max-w-2xl mx-auto">
            MirageBridge provides everything you need to build immersive VR experiences on Android devices running Termux.
          </p>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
          {features.map((feature) => (
            <div
              key={feature.title}
              className="group relative rounded-xl border border-border bg-card p-6 transition-all hover:border-primary/50 hover:bg-card/80"
            >
              <div className="flex h-12 w-12 items-center justify-center rounded-lg bg-primary/10 text-primary mb-4">
                <feature.icon className="h-6 w-6" />
              </div>
              <h3 className="text-lg font-semibold text-foreground mb-2">
                {feature.title}
              </h3>
              <p className="text-sm text-muted-foreground leading-relaxed">
                {feature.description}
              </p>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}
