import Link from "next/link";
import { Github } from "lucide-react";

export function Footer() {
  return (
    <footer className="border-t border-border bg-background py-12 px-4 sm:px-6 lg:px-8">
      <div className="mx-auto max-w-7xl">
        <div className="grid grid-cols-1 md:grid-cols-4 gap-8">
          {/* Brand */}
          <div className="md:col-span-2">
            <Link href="/" className="flex items-center gap-2 mb-4">
              <div className="flex h-8 w-8 items-center justify-center rounded-lg bg-primary">
                <span className="text-sm font-bold text-primary-foreground">MB</span>
              </div>
              <span className="text-lg font-semibold text-foreground">MirageBridge</span>
            </Link>
            <p className="text-sm text-muted-foreground max-w-md">
              Open-source VR bridge SDK for streaming head tracking and stereo frames from 
              Google Cardboard headsets to Termux applications.
            </p>
          </div>

          {/* Links */}
          <div>
            <h4 className="text-sm font-semibold text-foreground mb-4">Documentation</h4>
            <ul className="space-y-2">
              <li>
                <Link href="#features" className="text-sm text-muted-foreground hover:text-foreground">
                  Features
                </Link>
              </li>
              <li>
                <Link href="#architecture" className="text-sm text-muted-foreground hover:text-foreground">
                  Architecture
                </Link>
              </li>
              <li>
                <Link href="#setup" className="text-sm text-muted-foreground hover:text-foreground">
                  Quick Start
                </Link>
              </li>
              <li>
                <Link href="#sdk" className="text-sm text-muted-foreground hover:text-foreground">
                  SDK Reference
                </Link>
              </li>
            </ul>
          </div>

          {/* Resources */}
          <div>
            <h4 className="text-sm font-semibold text-foreground mb-4">Resources</h4>
            <ul className="space-y-2">
              <li>
                <Link
                  href="https://github.com/aquadeve/MirageBridge"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="flex items-center gap-2 text-sm text-muted-foreground hover:text-foreground"
                >
                  <Github className="h-4 w-4" />
                  GitHub
                </Link>
              </li>
              <li>
                <Link
                  href="https://github.com/aquadeve/MirageBridge/issues"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="text-sm text-muted-foreground hover:text-foreground"
                >
                  Report Issues
                </Link>
              </li>
              <li>
                <Link
                  href="https://github.com/aquadeve/MirageBridge/pulls"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="text-sm text-muted-foreground hover:text-foreground"
                >
                  Contribute
                </Link>
              </li>
            </ul>
          </div>
        </div>

        <div className="mt-12 pt-8 border-t border-border flex flex-col sm:flex-row items-center justify-between gap-4">
          <p className="text-sm text-muted-foreground">
            MirageBridge is open-source software.
          </p>
          <p className="text-sm text-muted-foreground">
            Built for the VR developer community.
          </p>
        </div>
      </div>
    </footer>
  );
}
