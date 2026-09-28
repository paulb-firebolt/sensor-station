import { defineConfig } from "blume";
import { z } from "zod";

export default defineConfig({
  title: "Sensor Station - People Counter",
  description:
    "Firmware documentation for an Ethernet-first people counting sensor platform with MQTT reporting",

  github: { owner: "paulb-firebolt", repo: "sensor-station", branch: "main" },

  theme: { mode: "system", accent: "teal" },

  // Pages carry `created` alongside Blume's built-in `lastModified`.
  frontmatter: {
    extend: {
      created: z.union([z.string(), z.date()]).optional(),
    },
  },

  lastModified: "git",

  navigation: {
    // Doxygen HTML is copied into public/doxygen by `npm run api`.
    featured: [
      { label: "Firmware API (Doxygen)", href: "/doxygen/index.html", icon: "code" },
    ],
    sidebar: [
      "/",
      "/ethernet-tls-and-security",
      "/m5stack-unit-poe-p4-wifi-setup",
      "/mqtt-implementation-plan",
      "/mqtt-command-reference",
      "/esp32-s3-ota-firmware-updates",
      "/wifi-provisioning-implementation",
      {
        label: "Requirements",
        items: [
          "/v3-requirements-gap-review",
          "/2026 PIR v3 Technical Requirements",
          "/2026 RAIS Base Station v3 Technical Requirements",
        ],
      },
      {
        label: "Development",
        items: [
          "/dev-environment",
          {
            label: "Active Development",
            items: [
              "/development/cc1310-config-packet-model",
              "/development/cc1310-version-and-sensor-type-reporting",
              "/development/cc1312r-functional-test",
              "/development/cc1312r-rf-coordinator",
              "/development/cc1312r-spi-interaction",
              "/development/cc1312r-uart-to-spi-migration",
              "/development/esp32-c6-wifi-coprocessor-plan",
              "/development/pirw-cc1310-migration-checklist",
              "/development/pirw-new-node-migration",
              "/development/pirw-phase-1-behavior-capture",
              "/development/pirw-phase-2-cc1312-team-handoff",
              "/development/pirw-phase-2-launchpad-simulated-telemetry",
            ],
          },
        ],
      },
      { label: "Notes", items: ["/ETHERNET_TLS_LIMITATION"] },
      {
        label: "Sensors",
        items: ["/thermal-occupancy-counter", "/ld2450-mmwave-sensor"],
      },
      {
        label: "RF",
        items: ["/bidirectional-rf-migration"],
      },
    ],
  },
});
