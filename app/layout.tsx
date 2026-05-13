import type { Metadata, Viewport } from "next";
import { Inter, JetBrains_Mono } from "next/font/google";
import "./globals.css";

const inter = Inter({
  subsets: ["latin"],
  variable: "--font-inter",
});

const jetbrainsMono = JetBrains_Mono({
  subsets: ["latin"],
  variable: "--font-jetbrains-mono",
});

export const metadata: Metadata = {
  title: "MirageBridge - VR Bridge SDK for Termux",
  description:
    "Stream head tracking and stereo frames from Google Cardboard headsets to Termux applications. Build VR apps on mobile with native OpenXR integration.",
  keywords: [
    "VR",
    "Virtual Reality",
    "Termux",
    "Android",
    "Google Cardboard",
    "OpenXR",
    "SDK",
    "Head Tracking",
  ],
  authors: [{ name: "MirageBridge Team" }],
  openGraph: {
    title: "MirageBridge - VR Bridge SDK for Termux",
    description:
      "Stream head tracking and stereo frames from Google Cardboard headsets to Termux applications.",
    type: "website",
  },
};

export const viewport: Viewport = {
  themeColor: "#0a0a0a",
  width: "device-width",
  initialScale: 1,
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className={`${inter.variable} ${jetbrainsMono.variable} bg-background`}>
      <body className="font-sans antialiased">{children}</body>
    </html>
  );
}
