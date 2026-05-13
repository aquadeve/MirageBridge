import { Header } from "@/components/header";
import { Hero } from "@/components/hero";
import { Features } from "@/components/features";
import { Architecture } from "@/components/architecture";
import { Setup } from "@/components/setup";
import { SDKReference } from "@/components/sdk-reference";
import { Footer } from "@/components/footer";

export default function Home() {
  return (
    <main className="min-h-screen">
      <Header />
      <Hero />
      <Features />
      <Architecture />
      <Setup />
      <SDKReference />
      <Footer />
    </main>
  );
}
