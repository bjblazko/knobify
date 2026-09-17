# Architecture Decision Records

Lightweight MADR-style ADRs. One numbered file per decision, in
chronological order. Once accepted, an ADR is not edited to reflect new
thinking — a later decision that changes course gets its own new ADR that
supersedes the old one (and the old one is marked "Superseded by NNNN").

## Index

| # | Title | Status |
|---|-------|--------|
| [0001](0001-language-and-framework-choice.md) | Language and framework choice | Accepted |
| [0002](0002-v1-format-and-mcu-scope.md) | v1 format and MCU scope | Accepted (format part superseded by 0016) |
| [0003](0003-testing-strategy.md) | Testing strategy | Accepted |
| [0004](0004-navigation-library-and-index-architecture.md) | Navigation, library indexing, and index cache architecture | Accepted |
| [0005](0005-power-lock-and-round-edge-indicators.md) | Device lock, display power, and round-edge UI widgets | Accepted |
| [0006](0006-audio-task-concurrency.md) | Dedicated FreeRTOS task for audio decode | Accepted |
| [0007](0007-battery-indicator.md) | Battery indicator | Accepted (placement superseded by 0008) |
| [0008](0008-braun-design-system-and-screen-redesign.md) | Braun design system and screen redesign | Accepted (Now Playing no-cover layout amended by 0009) |
| [0009](0009-now-playing-spectrum-analyzer.md) | Now Playing spectrum analyzer | Accepted |
| [0010](0010-main-menu-and-settings.md) | Main menu, settings, and display brightness | Accepted |
| [0011](0011-shuffle-and-repeat.md) | Shuffle and repeat | Accepted |
| [0012](0012-resume-session.md) | Resume where you left off | Accepted |
| [0013](0013-jog-shuttle.md) | Jog/shuttle on Now Playing | Accepted (amends 0008's one-edge-ring rule) |
| [0014](0014-now-playing-options-panel.md) | Now Playing options panel | Accepted (amends 0005, 0009, 0011) |
| [0015](0015-sleep-timer.md) | Sleep timer | Accepted (amends 0010's Home layout) |
| [0016](0016-native-formats-and-usb-drive.md) | Native M4A, progressive covers, USB drive mode | Accepted (supersedes 0002's format decision) |
| [0017](0017-two-audio-decode-paths.md) | Two audio decode paths (Vorbis) | Accepted |
| [0018](0018-collections-and-menu-visibility.md) | Collections, Home carousel, configurable main menu | Accepted |
| [0019](0019-utf8-tag-text-and-project-text-fonts.md) | UTF-8 tag text and project-generated text fonts | Accepted (supersedes 0008's and 0013's ASCII notes) |
| [0020](0020-patching-esp32-audioi2s-m4a-seek.md) | Patching ESP32-audioI2S at build time to fix M4A seeking | Accepted (amends 0001) |
