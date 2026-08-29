# Feature Specification: Fix Web Page Internationalization

**Feature Branch**: `004-fix-web-i18n`

**Created**: 2026-07-12

**Status**: Draft

**Input**: User description: "fix i18n of web pages. maby better reinplement method"

## Problem Statement

The original firmware specification (001, FR-022) requires the captive portal setup page to be fully localized in English, German, French, and Ukrainian, selected automatically from the browser's language preference. This requirement is currently not met: regardless of the browser's language settings, all web pages are displayed in English only. Translation content for all four languages exists on the device but is never delivered to the browser, and the mechanism that should tell the page which language was selected is incomplete. The management page has no localization at all. The localization mechanism needs to be repaired — or replaced with a more robust approach — so that the promised multilingual experience actually works.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Setup portal appears in the user's language (Priority: P1)

A user whose browser is configured for German, French, or Ukrainian connects to the weather station's setup network and opens the setup portal. Every visible element of the page — title, headings, field labels, placeholders, buttons, and all status and error messages that appear during scanning and connecting — is displayed in their language. A user with any other browser language (or no language preference) sees the complete page in English.

**Why this priority**: This is the original, still-unmet product requirement. The setup portal is the first thing every new owner of the device sees, and non-English speakers currently get an English-only experience despite translations existing on the device.

**Independent Test**: Set the browser's preferred language to German (or French/Ukrainian), open the setup portal, and confirm every visible string is in that language, including the status messages shown while scanning for networks and while connecting. Repeat with an unsupported language (e.g., Japanese) and confirm complete English text.

**Acceptance Scenarios**:

1. **Given** a browser whose preferred language is German, **When** the user opens the setup portal, **Then** all static page text (title, heading, labels, placeholders, buttons) is displayed in German on first load.
2. **Given** the portal is displayed in French, **When** the user scans for networks or submits credentials, **Then** all resulting status and error messages (scanning, connecting, success, wrong password, network not found, generic failure) appear in French.
3. **Given** a browser whose preferred language is unsupported (e.g., Japanese), **When** the user opens the setup portal, **Then** the complete page is displayed in English with no missing or blank text.
4. **Given** a browser with a regional language variant (e.g., Austrian German "de-AT") or a weighted preference list (e.g., "fr-FR, fr;q=0.9, en;q=0.8"), **When** the user opens the portal, **Then** the best matching supported language is chosen (German and French respectively).

---

### User Story 2 - Management page appears in the user's language (Priority: P2)

After the station is set up, the user opens the management page from their home network. The page — including sensor readouts' labels, time/WiFi status descriptions, configuration controls, firmware update messages, and history view — is displayed in the same language the setup portal would choose for that browser.

**Why this priority**: The management page is the page users interact with long-term, and "web pages" in the request covers it; however it was never localized, so this is an extension of the original scope rather than a regression fix. The device is fully usable in English without it.

**Independent Test**: Set the browser's preferred language to Ukrainian and open the management page; confirm all visible labels, statuses, and messages (including dynamically generated ones such as update progress and record counts) appear in Ukrainian.

**Acceptance Scenarios**:

1. **Given** a browser whose preferred language is Ukrainian, **When** the user opens the management page, **Then** all static labels and section headings are displayed in Ukrainian.
2. **Given** the management page is displayed in German, **When** a firmware update is performed or the history table is loaded, **Then** dynamically generated messages (progress, success, failure, record counts, "no sync") appear in German.
3. **Given** a browser with an unsupported preferred language, **When** the user opens the management page, **Then** the complete page is displayed in English.

---

### User Story 3 - Graceful behavior when translations cannot be delivered (Priority: P3)

If the translated text cannot be obtained for any reason, the user still sees a complete, fully functional page in English — never blank labels, untranslated placeholders, or broken controls.

**Why this priority**: Safety net. It protects the setup flow (the device's critical path) from being blocked by a localization failure, but it only matters in rare failure situations.

**Independent Test**: Simulate a failure to deliver translation content (e.g., via the hardware emulator) and confirm the portal renders entirely in English and the setup flow can be completed.

**Acceptance Scenarios**:

1. **Given** translation content for the chosen language cannot be loaded, **When** the page renders, **Then** every visible element shows its English text and all functions (scan, connect, configure) work normally.
2. **Given** a single translated string is missing from an otherwise available language, **When** the page renders, **Then** that specific element shows its English text while the rest of the page remains in the chosen language.

---

### Edge Cases

- Browser sends no language preference at all → English is used.
- Browser sends a long preference list where the only supported language has a low weight (e.g., "ja, zh;q=0.9, uk;q=0.2") → the supported language (Ukrainian) is still chosen over the fallback.
- Browser sends a preference list with only unsupported languages → English.
- Translated strings are substantially longer than English (typical for German and Ukrainian) → page layout remains readable and controls remain usable on a phone-sized screen.
- Translation content and page content get out of sync (a new UI string exists but a translation doesn't) → the English text is shown for that string; the page never shows a raw key name or blank element.
- The language decision must be consistent between what the page displays and what the device reports (a page must not announce one language while displaying another).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The setup portal page MUST render all user-visible text — static content (title, headings, labels, placeholders, buttons) and dynamic content (status and error messages) — in one of the supported languages: English (en), German (de), French (fr), Ukrainian (uk).
- **FR-002**: The displayed language MUST be selected automatically from the browser's language preference, honoring preference order and weights, and matching regional variants to their base language (e.g., "de-AT" → German). English MUST be used when no supported language matches or no preference is sent.
- **FR-003**: The language selection MUST take effect on the first page load without any user action.
- **FR-004**: The management page MUST be localized in the same four languages using the same automatic selection behavior as the setup portal.
- **FR-005**: When translation content for the selected language cannot be obtained, the page MUST render completely in English and remain fully functional.
- **FR-006**: When an individual translated string is missing, the corresponding English string MUST be displayed in its place; raw identifiers or empty elements MUST never be shown to the user.
- **FR-007**: Every user-visible string on the localized pages MUST have a translation entry in all four languages, and the sets of translatable strings MUST be verifiably consistent across languages (same string inventory per language).
- **FR-008**: The page MUST declare the language it is actually displayed in (so browsers, screen readers, and translation tools treat the content correctly).

### Key Entities

- **Language pack**: The set of translated strings for one supported language; one pack exists per language (en, de, fr, uk), each containing the same inventory of string identifiers.
- **Language preference**: The ordered, weighted list of languages the browser sends with each request; input to the automatic selection.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For each of the four supported languages, a user whose browser prefers that language sees 100% of the visible text on the setup portal in that language on first load, including all status messages reachable during the setup flow.
- **SC-002**: For each of the four supported languages, a user whose browser prefers that language sees 100% of the visible text on the management page in that language, including dynamically generated messages.
- **SC-003**: A user with an unsupported browser language sees a complete English page with zero blank, missing, or identifier-only text elements.
- **SC-004**: Language preference lists with regional variants and weights (at least: "de-AT", "fr-FR,fr;q=0.9,en;q=0.8", "uk,en;q=0.5", "ja, zh;q=0.9, uk;q=0.2") each resolve to the expected language on a real page load, verified end-to-end rather than only in isolated unit tests.
- **SC-005**: With translation delivery deliberately broken, a user can still complete the full device setup flow in English without errors caused by localization.
- **SC-006**: An automated check can confirm that all four language packs contain identical string inventories and that every string used by the pages exists in the packs; the check reports zero discrepancies.

## Assumptions

- The four languages from the original specification (en, de, fr, uk) remain the complete target set; no new languages are added by this feature.
- Automatic selection from the browser's language preference remains the chosen approach; a manual language switcher on the pages is out of scope for this feature (it can be specified separately if desired).
- The user's phrase "maybe better reimplement method" is taken as permission to replace the current localization delivery mechanism entirely if repairing it is not the most robust option; the choice of mechanism is a planning/implementation decision, and this specification constrains only the observable behavior.
- "Web pages" includes both the setup portal and the management page; the management page localization (User Story 2) is new scope beyond the original requirement and can be delivered after the portal fix.
- Existing translation content for the portal (already present on the device in all four languages) is assumed accurate and reusable; new translations are needed only for strings that lack them (notably the management page).
- The device's hardware emulator can be used to verify browser-facing behavior end-to-end without physical hardware.
