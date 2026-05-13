"use client";

import { useState } from "react";
import { Check, Copy, Smartphone, Terminal } from "lucide-react";
import { cn } from "@/lib/utils";

const steps = [
  {
    id: "prerequisites",
    title: "Prerequisites",
    content: [
      {
        type: "list",
        items: [
          "Android device with Google Cardboard compatible headset",
          "Termux app installed from F-Droid (not Play Store)",
          "Android SDK with NDK (for building)",
          "CMake 3.18+ installed",
        ],
      },
    ],
  },
  {
    id: "clone",
    title: "Clone Repository",
    content: [
      {
        type: "code",
        language: "bash",
        code: `git clone https://github.com/aquadeve/MirageBridge.git
cd MirageBridge`,
      },
    ],
  },
  {
    id: "android",
    title: "Build Android App",
    content: [
      {
        type: "text",
        text: "Set your Android SDK path and build the GVR host application:",
      },
      {
        type: "code",
        language: "bash",
        code: `export ANDROID_SDK_ROOT=/path/to/android/sdk
./scripts/build-android.sh`,
      },
      {
        type: "text",
        text: "Install the APK on your device:",
      },
      {
        type: "code",
        language: "bash",
        code: `./scripts/deploy-android.sh`,
      },
    ],
  },
  {
    id: "termux",
    title: "Build Termux Components",
    content: [
      {
        type: "text",
        text: "In Termux on your Android device, install build dependencies:",
      },
      {
        type: "code",
        language: "bash",
        code: `pkg install cmake clang git`,
      },
      {
        type: "text",
        text: "Clone and build the Termux components:",
      },
      {
        type: "code",
        language: "bash",
        code: `git clone https://github.com/aquadeve/MirageBridge.git
cd MirageBridge
./scripts/build-termux.sh`,
      },
    ],
  },
  {
    id: "run",
    title: "Run MirageBridge",
    content: [
      {
        type: "text",
        text: "Start the daemon in Termux:",
      },
      {
        type: "code",
        language: "bash",
        code: `./termux/build/miragebridge-daemon/miragebridge-daemon`,
      },
      {
        type: "text",
        text: "Launch the Android app and insert your phone into the VR headset. The daemon will begin receiving tracking data.",
      },
    ],
  },
];

function CodeBlock({ code, language }: { code: string; language: string }) {
  const [copied, setCopied] = useState(false);

  const handleCopy = async () => {
    await navigator.clipboard.writeText(code);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="relative group">
      <pre className="rounded-lg border border-code-border bg-code-bg p-4 overflow-x-auto">
        <code className="text-sm text-muted-foreground font-mono">{code}</code>
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

export function Setup() {
  const [activeStep, setActiveStep] = useState(0);

  return (
    <section id="setup" className="py-20 px-4 sm:px-6 lg:px-8">
      <div className="mx-auto max-w-7xl">
        <div className="text-center mb-16">
          <h2 className="text-3xl sm:text-4xl font-bold text-foreground">
            Quick Start Guide
          </h2>
          <p className="mt-4 text-lg text-muted-foreground max-w-2xl mx-auto">
            Get MirageBridge running on your device in just a few steps.
          </p>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-4 gap-8">
          {/* Step Navigation */}
          <div className="lg:col-span-1">
            <div className="sticky top-24 space-y-2">
              {steps.map((step, index) => (
                <button
                  key={step.id}
                  onClick={() => setActiveStep(index)}
                  className={cn(
                    "w-full text-left px-4 py-3 rounded-lg transition-all",
                    activeStep === index
                      ? "bg-primary/10 border border-primary/30 text-foreground"
                      : "text-muted-foreground hover:text-foreground hover:bg-secondary"
                  )}
                >
                  <div className="flex items-center gap-3">
                    <span
                      className={cn(
                        "flex h-6 w-6 items-center justify-center rounded-full text-xs font-medium",
                        activeStep === index
                          ? "bg-primary text-primary-foreground"
                          : "bg-secondary text-muted-foreground"
                      )}
                    >
                      {index + 1}
                    </span>
                    <span className="text-sm font-medium">{step.title}</span>
                  </div>
                </button>
              ))}
            </div>
          </div>

          {/* Step Content */}
          <div className="lg:col-span-3">
            <div className="rounded-xl border border-border bg-card p-6 sm:p-8">
              <div className="flex items-center gap-3 mb-6">
                {activeStep === 0 && <Smartphone className="h-6 w-6 text-primary" />}
                {activeStep > 0 && activeStep < 4 && <Terminal className="h-6 w-6 text-primary" />}
                {activeStep === 4 && <Check className="h-6 w-6 text-primary" />}
                <h3 className="text-xl font-semibold text-foreground">
                  {steps[activeStep].title}
                </h3>
              </div>

              <div className="space-y-4">
                {steps[activeStep].content.map((block, index) => {
                  if (block.type === "text") {
                    return (
                      <p key={index} className="text-muted-foreground">
                        {block.text}
                      </p>
                    );
                  }
                  if (block.type === "code") {
                    return <CodeBlock key={index} code={block.code} language={block.language} />;
                  }
                  if (block.type === "list") {
                    return (
                      <ul key={index} className="space-y-2">
                        {block.items.map((item, i) => (
                          <li key={i} className="flex items-start gap-2 text-muted-foreground">
                            <Check className="h-5 w-5 text-primary shrink-0 mt-0.5" />
                            <span>{item}</span>
                          </li>
                        ))}
                      </ul>
                    );
                  }
                  return null;
                })}
              </div>

              {/* Navigation */}
              <div className="flex justify-between mt-8 pt-6 border-t border-border">
                <button
                  onClick={() => setActiveStep(Math.max(0, activeStep - 1))}
                  disabled={activeStep === 0}
                  className="px-4 py-2 text-sm text-muted-foreground hover:text-foreground disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  Previous
                </button>
                <button
                  onClick={() => setActiveStep(Math.min(steps.length - 1, activeStep + 1))}
                  disabled={activeStep === steps.length - 1}
                  className="px-4 py-2 text-sm bg-primary text-primary-foreground rounded-lg hover:bg-primary/90 disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  Next Step
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
