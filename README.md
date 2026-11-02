<p align="center">
  <img src="/assets/Koan-Logo.svg" width="200" height="200" alt="Koan">
</p>
<img src="https://numerio.goatcounter.com/count?p=/koan-readme" alt="" width="0" height="0">

**Koan Operating System** is a fork of Vitruvian (VitruvianOS, or **V\OS**) focused on freedesktop standards interoperability. It brings the simplicity of a modern BeOS-inspired system and the power of Linux to modern hardware, while speaking the native language of the Linux desktop: D-Bus, Wayland, XDG specs, and the freedesktop ecosystem.You should consider this a linux distribution of VitruvianOS.

#### Goals

* **Freedesktop standards interoperability** — D-Bus services, desktop entries, MIME applications, notifications, system tray, MPRIS, secret storage, trash, and more, working natively alongside the Vitruvian stack.
* **Fast, reactive, and easy-to-use interface** — Minimal latency from input to response, intuitive navigation, no bloat.
* **Highly integrated Desktop Environment** — The desktop, applications, and system services work as a coherent whole.
* **Wayland integration (WIP)** — Wayland support in the Haiku-like environment so Linux Wayland applications can integrate and run natively on Koan.
* **Fractional scaling (WIP)** — Per-output fractional display scaling for sharp UI on mixed-DPI and high-resolution screens.
* **Modern window decorations (WIP)** - Modern ui for the current and next generations to feel at home.
* **Haiku App Compatibility** - Keep the haiku app compatibility matching at least the VitruvianOS one.
* **Make our own Wordmark** - We don't currently have a Koan svg Wordmark.

---

> **⚠️ Alpha — not ready for production use**
>
> Koan is an **early alpha** operating system. It is **not suitable for production environments**.
> Some debugging and diagnostic options remain enabled even in release ISOs, and it has not
> been security-hardened. **Do not run Koan on untrusted network environments.**

---

<img width="1646" height="951" alt="The Koan desktop (WIP)" src="/desktop.png" />

### Overview

Koan runs on Linux and takes its look and programming model from BeOS. You get Linux hardware support with a desktop that stays responsive. BeOS/Haiku applications build and run with few or no source changes. On top of that foundation, Koan adds freedesktop standards so Vitruvian applications and services interoperate with the wider Linux desktop.

We ship custom kernel modules and, on amd64, a PREEMPT_RT kernel to keep latency low. The regular kernel works too.

Images boot from ext4, and the live ISO uses SquashFS. XFS and Btrfs also work.

Release images are built for amd64 and arm64, and the arm64 ones include the Raspberry Pi 4 and 5. There's a riscv64 port in the nightly builds. See the [boards site](https://boards.v-os.dev/) (VitruvianOS) for the full list , For Koan itself We only have an amd64 machine so I will only release amd64 in the meanwhile (Check github releases for Koan itself).

### Screen Scaling (HiDPI)

Koan supports fractional per-display scaling so the UI stays sharp on mixed-DPI and high-resolution screens. Scaling is applied
consistently to window bounds, title bars, tabs, controls, fonts, bitmaps,
clipping/damage regions, cursor coordinates, hit tests, resize borders, and
drag-and-drop. Pointer coordinates are transformed in the inverse direction from
rendering so input stays aligned with what you see.

The deskbar and native applications scale automatically with the display. When
moving a window between displays with different scale factors, its logical size
stays stable.

> **⚠️ Browser web content is not scaling-aware**
> >
> > Web content rendered by the browser (e.g. NetPlus) does not automatically
> > respond to system screen scaling. Web pages render at CSS pixels and are not
> > resized by the display scale factor. To make web content larger or smaller,
> > use the browser's manual zoom (Ctrl++ / Ctrl− / Ctrl+0) instead.

---

### Nexus

Nexus is a set of Linux kernel modules that give Koan its BeOS-style primitives, ports and node monitoring among them. It's a submodule at `src/system/kernel/nexus` and ships as the `nexus-dkms` package. More in the [Nexus repository](https://github.com/VitruvianOS/Nexus) (VitruvianOS).

### Talk to the people who use it

* [Discourse](https://discourse.v-os.dev/), the forum (VitruvianOS)
* [Telegram chat](https://t.me/vitruvian_official_chat), plus a [channel](https://t.me/vitruvian_official) for announcements (VitruvianOS)
* [GitHub issues](https://github.com/VitruvianOS/Vitruvian/issues) for bugs (VitruvianOS)
* The [mailing list](https://www.freelists.org/list/vitruvian), if you like email
* Don't annoy Vitruvian Mantainers with Koan specific bugs

### Installation

Grab an image from Github Releases , write it to a USB stick and boot it. The [installation guide](https://docs.v-os.dev/installation/) (VitruvianOS) has the details. If you'd rather not touch real hardware yet, [run it in a VM](https://docs.v-os.dev/getting-started/virtualization/) (VitruvianOS) .

### Getting started

* [Building from source](https://docs.v-os.dev/getting-started/building/) (VitruvianOS)
* [Coding guidelines](https://docs.v-os.dev/development/coding-guidelines/) (VitruvianOS)
* [Filesystem layout](https://docs.v-os.dev/development/filesystem-layout/) (VitruvianOS)
* [All the docs](https://docs.v-os.dev/) (VitruvianOS)
* [Packages](https://packages.v-os.dev/) (VitruvianOS)

Pull requests are welcome. Have a look at the coding guidelines first.
We will accept AI code contributions in the future as long as they are generated by GPT-6 (Luna,Astra) or Claude Opus 5.5.

### Trademark Notice
Koan Operating System is an independent operating system project derived from VitruvianOS. Koan Operating System is not affiliated with, endorsed by, sponsored by, or officially maintained by the VitruvianOS project or its developers.

VitruvianOS is the name of the upstream project. Its name, branding, and associated marks belong to their respective rights holders.

Debian® is a registered trademark of Software in the Public Interest, Inc. (SPI), managed by the Debian Project.

Linux® is the registered trademark of Linus Torvalds in the United States and other countries.

GPT-6 is a trademark of OpenAI OpCo, LLC.

Claude and Claude Opus are trademarks of Anthropic, PBC.

All trademarks, product names, logos, and company names are the property of their respective owners. Their use in this project is for identification and attribution purposes only and does not imply endorsement, sponsorship, affiliation, or partnership with Koan Operating System.

### SSH debug boot

Build with `./configure --sshdebug` to include the opt-in SSH debug entry and
the guest-local MCP diagnostics endpoint. The MCP server is reachable through
an SSH local forward; see the [SSH Debug MCP guide](docs/ssh-debug-mcp.md).

### Donate

Koan is built by volunteers. There's is not current dontation page for Koan itself, but you can donate to Vitruvian main project as is the basis for Koan [Donate](https://v-os.dev/donate/) page.

### License

Koan contains components licensed under the [GPL](https://www.gnu.org/licenses/gpl-3.0.html), [MIT](https://opensource.org/licenses/MIT), and [Apache-2.0](data/system/data/licenses/Apache%20v2). Check each component's file and package notices for its applicable license.
