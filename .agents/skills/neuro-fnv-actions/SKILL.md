---
name: neuro-fnv-actions
description: Use when changing NeuroSDK context, actions, action registration/forcing/results, JSON schemas, menu automation, or frame-driven visual action execution in neuro-FNV.
compatibility: OpenCode project skill for the neuro-FNV 32-bit xNVSE plugin
metadata:
  project: neuro-FNV
  domain: actions-and-menu-automation
---

# Neuro-FNV Actions

Use this skill before working on files under `src/Actions/`, adding menu-backed Neuro actions, changing
`NeuroSDK` action transport, or designing action execution and UI lockout behavior.

The SDK-facing design guidance below is derived from VedalAI's official
[`API/BEST_PRACTICES.md`](https://github.com/VedalAI/neuro-sdk/blob/main/API/BEST_PRACTICES.md). Its protocol behavior
is verified against the current server; guidance about what Neuro handles well is directional and may change. Project
invariants and locally verified implementation details in this skill remain mandatory.

## Start Here

Read these files before editing:

- `src/Actions/Action.hpp`: action definitions, requests, validation output, execution, and revalidation.
- `src/Actions/ActionData.*`: strict parsing of incoming stringified JSON through libneurosdk's vendored `json.h`.
- `src/Actions/ActionRegistry.*`: active action lookup, dispatch ordering, result retry, and execution start.
- `src/Actions/ActionWindow.*`: client-only action grouping, registration, forcing, and teardown.
- `src/Actions/Json/JsonSchema.*`: typed simple JSON Schema builder and serializer.
- `src/NeuroSDK.*`: the only layer that should call the libneurosdk poll/send/destroy API.
- `src/MenuHandler.cpp` and `src/Menus/`: menu observation, precedence, gameplay blocking, and visual executors.
- `src/Actions/Menu/SetSpecialAction.*` and `src/Menus/SpecialAllocationHandler.*`: first complete reference action.

## Neuro Protocol Constraints

The game sends messages shaped as `{command, game, data?}`. Neuro sends `{command, data?}`.

Supported outgoing commands used here:

| Purpose | Wire command | C wrapper |
| --- | --- | --- |
| Startup | `startup` | `NeuroSDK::StartupMessage()` |
| Context | `context` | `NeuroSDK::SendContext()` |
| Register | `actions/register` | `NeuroSDK::RegisterActions()` |
| Unregister | `actions/unregister` | `NeuroSDK::UnregisterActions()` |
| Force | `actions/force` | `NeuroSDK::ForceActions()` |
| Result | `action/result` | `NeuroSDK::SendActionResult()` |

Important invariants:

- An incoming action's `id` must be copied and used for `action/result`. Never use the action name as the result ID.
- Incoming `data` is nullable stringified JSON. Treat it as untrusted and validate it locally.
- Advertised schemas guide Neuro but do not replace local validation.
- Send an action result immediately after validation/revalidation and before game execution.
- A successful pre-execution result means "accepted for execution", not "completed".
- `success: false` retries an active action force. Return a useful correction message with current valid options.
- Neuro can process only one action force at a time. The current project has one forced window; add explicit global
  coordination before introducing concurrently forceable systems.
- Action windows do not exist on the server or in libneurosdk. They are entirely client-side.
- Keep stable action names as state changes. Put current choices in context, force state/query, and action arguments.
- Project action names currently enforce lowercase letters and underscores with no leading/trailing underscore.

libneurosdk is intentionally a thin C transport. Do not expand it unless a wire bug cannot be worked around. The local
submodule currently contains one required critical fix: `NeuroSDK_MessageKind_ActionResult` must serialize
`action/result`, not `action:result`.

## Official SDK Integration Practices

Apply these rules when designing the user-facing integration, not only when serializing protocol messages.

### Context

- Write Markdown or readable plaintext. Prefer Markdown, start structured content at `##`, and avoid top-level `#`
  headings. Do not send raw JSON when a short explanation or bulleted list communicates the same state.
- Send occasional, meaningful updates. Never stream small position, animation, frame, or other high-frequency changes.
- Send long-lived rules and controls once at startup and again only at meaningful boundaries such as a new level or
  first encounter. Put information needed for the immediate forced decision in that force's `state`.
- Context may be sent while an action result is pending; the server queues it and preserves delivery order. Still send
  the result first whenever possible so Neuro is not blocked waiting.
- Give Neuro only information available to a human player. Moderate explicit lists (roughly 20 items) are acceptable;
  clearly state their meaning instead of expecting inference.

### Action Surface

- Use descriptive verb names and descriptions of one or two sentences. Neuro reads the exact `name`, `description`,
  and `schema`; do not add an action that merely lists currently registered actions.
- Keep persistent gameplay actions registered and stable. Avoid rapid registration churn because it delays responses.
  Menu-scoped `ActionWindow`s are the justified exception: publish once when the menu becomes actionable, retain the
  window while waiting, and close it exactly once on acceptance or menu invalidation.
- Use separate actions for genuinely different verbs. Use one action with an `enum` for a fixed homogeneous choice
  set. For frequently changing choices, keep a stable broad parameter, validate it locally, and return the current
  valid options on failure instead of rebuilding an enum every frame.
- Avoid many near-identical actions; Neuro tends to fixate on a subset. Action names are character-scoped and shared
  by concurrent integrations, so choose collision-resistant names if neuro-FNV must coexist with another integration.
- Always return `success: false` for unknown or stale action names. The server safely discards results for stale IDs;
  silence leaves Neuro blocked.

### Results and Validation

- Return `action/result` immediately after local validation and before in-game execution. The current server abandons
  actions after a short timeout of roughly 20 seconds, but this is not a timer to rely on.
- Treat schema-conforming data as untrusted anyway. Neuro may request impossible actions or send malformed values;
  validate shape, value, current availability, permissions, and live state.
- A failed result during an active force is retried automatically a limited number of times. Make its message
  actionable and include the current valid choices. Outside a force, there is no automatic retry.
- Successful results are empty in this project because validation cannot mutate game state and execution starts only
  after result delivery. Report meaningful deferred state changes through context; never imply execution completed.

### Forces

- Force an action whenever gameplay is blocked on Neuro's decision. For open-ended situations, allow a reasonable
  period and then force instead of trusting Neuro to remember indefinitely.
- Maintain exactly one active force. Wait for its result before issuing the next; a new force can cancel and replace
  the current one.
- Use `low` for ordinary turn-based decisions. Use `medium` or `high` only when waiting harms time-sensitive gameplay.
  Reserve `critical` for expiring hard-real-time decisions because it interrupts Neuro mid-sentence.
- Write `query` and `state` as concise Markdown. Set `ephemeral_context: true` for bulky state repeated every turn so
  it applies only to that decision.
- Chained forces are valid but each adds a full response round-trip. Prefer one action with two or three parameters
  when those choices form one decision.

### Reconnection

- After reconnecting, immediately send `startup` again and re-register the complete current action set.
- Previously sent context survives a disconnect. Do not replay old context unless the game state makes repetition
  useful.

## Polling and Ownership

`NeuroSDK::MainLoop()` is the main-thread boundary:

```text
poll and copy incoming messages
-> MenuHandler::Process()
-> ActionRegistry::Dispatch(owned requests)
-> Walker::Stop() or Walker::Process()
```

Rules:

- Copy incoming `id`, `name`, and `data` into `Actions::Request` immediately.
- Call `neurosdk_message_destroy()` exactly once for every polled action message.
- Never retain pointers into libneurosdk's returned message array.
- `neurosdk_context_send()` polls internally. `SendSDKMessage()` drains messages into `actionInbox`; it must not
  recursively dispatch them.
- Dispatch an owned inbox snapshot. Messages received while sending results are handled on a later boundary.
- `ResetAutomation()` clears menus, registry entries, pending results, the inbox, and Walker input on disconnect/load.
- Lifecycle reset is called for preload, new game, and exit to main menu in `src/plugin.cpp`.

## User-Facing Action Surface

Each concrete action implements `Actions::IAction`:

```cpp
class SomeAction final : public Actions::IAction {
public:
  const Actions::Definition &GetDefinition() const override;
  Actions::PreparedAction Validate(const Actions::Request &request) override;
};
```

`Actions::Definition` contains:

```cpp
struct Definition {
  std::string name;
  std::string description;
  Json::JsonSchema schema;
};
```

`PreparedAction::Failure(message)` rejects without execution. A forced window remains available so Neuro can retry.

`PreparedAction::Success(execute, revalidate)` carries:

- A deferred execution callback.
- An optional fresh-state revalidation callback.
- No result message: no game state has changed before dispatch sends the successful result.

Validation must not mutate game state. Parse and validate into owned typed values, then capture those values in the
prepared execution callback.

Use revalidation whenever state can change between receipt and execution. Capture stable facts such as menu owner,
page, point total, form/reference identity, or scope generation. A failed revalidation sends `success: false` and does
not execute.

## Dispatch Ordering

`ActionRegistry::Dispatch()` follows this order:

```text
lookup active action
-> parse and validate
-> revalidate live state
-> close/unregister its ActionWindow
-> send success result
-> execute prepared callback
```

Failure behavior:

- Unknown/unregistered name: send a failed result.
- Validation failure: send a failed result and keep the window open.
- Revalidation failure: send a failed result and do not execute.
- Window unregister failure: do not execute; keep the window in retryable `Closing` state.
- Success-result send failure: retain the prepared callback in `PendingResult`, revalidate later, resend the result,
  then execute only after the result succeeds.

Local unbinding happens before network unregister to prevent duplicate execution while libneurosdk polls internally.

## Action Windows

`ActionWindow` states:

```text
Building -> Registered -> Forced -> Closing -> Ended
               |             |
               +-----------> Faulted
```

Typical menu setup:

```cpp
window.Add(std::make_unique<Actions::Menu::SomeAction>())
    .SetForce(query, state, NeuroSDK::ActionPriority::High);

if (!window.Register()) {
  // Retain Closing windows so unregister can retry; discard other failed windows.
}
```

Registration performs:

```text
bind actions locally
-> send actions/register
-> send optional actions/force with decision-scoped ephemeral state
```

`End()` locally unbinds first, sends `actions/unregister`, and remains `Closing` if the send fails. `Abandon()` is for
local reset during disconnect/load when server notification cannot be trusted.

Only one `ActionWindow` may own a force at a time. The process-wide coordinator in `ActionWindow.cpp` rejects a second
forced window until the current owner ends or abandons its force.

An action belongs to one owning window object. The window must outlive registered actions and any in-flight dispatch.
Menu handlers currently own windows through `std::unique_ptr`.

## JSON Schema Surface

Never embed a raw schema JSON string in an action. Build `Actions::Json::JsonSchema` values.

Example:

```cpp
auto schema = Actions::Json::JsonSchema::Object();

auto index = Actions::Json::JsonSchema::Integer();
index.Minimum(0).Maximum(maxIndex);
schema.Property("enemy", std::move(index));
```

Supported builder keywords:

- `type`, `properties`, `required`, and `items`.
- `enum` and `const`.
- `minimum`, `maximum`, `exclusiveMinimum`, and `exclusiveMaximum`.
- `minLength`, `maxLength`, `pattern`, and `format`.
- `minItems`, `maxItems`, and `uniqueItems`.

Action registration rejects a non-empty root schema unless its type is `object`. Parameterless actions use an empty
default `JsonSchema`, serialized as `{}`.

Do not add unsupported/poorly supported keywords such as `additionalProperties`, composition (`oneOf`, `anyOf`,
`allOf`), references, conditional schemas, property-count constraints, metadata keywords, or unevaluated/dependent
keywords. `uniqueItems` is advertised only; always enforce uniqueness locally too.

Share property-name constants between schema construction and validation to prevent drift. `SetSpecialAction` uses one
`kNames` array for both.

`ActionData` currently provides object checks, property count, and strict integer extraction. Extend this abstraction
for new payload types rather than calling `json.h` directly from every action.

## Menu Integration and Execution Lockout

Keep menu observation/window publication separate from visual execution.

A menu-backed handler generally owns:

- The currently published `ActionWindow`.
- A frame-driven execution state.
- A lockout flag/state implied by active execution.
- Captured menu identity and values used by validation/revalidation.

Recommended flow:

```text
menu reaches actionable page
-> send context/register/force once
-> wait without regenerating the window
-> accepted action closes window
-> execution lockout starts next frame
-> drive visible controls over time
-> observe menu closure/completion
-> clear lockout and resume Walker
```

During execution:

- Do not publish a replacement action window because pages/options changed due to the action itself.
- Return a blocking state from the menu handler so Walker does not inject movement.
- Pause under a higher-priority popup rather than abandoning valid work immediately.
- Re-read engine state after every visible click; do not assume the click succeeded.
- Prefer menu `HandleClick` through `UIUtils::GetControl`, `ClickControl`, and `ClickTile` over direct internal writes.
- Use delayed, one-step visual operations so livestream viewers can follow the UI.
- Clear lockout based on actual top/active menu state. Engine singleton pointers may remain non-null after closure.
- Distinguish a retained singleton from an open menu using `Interface::GetTopMenuID()` and menu mode/active-menu state.
- Provide bounded timeout and visible recovery for stuck execution.

`MenuHandler::Process()` always observes subtitles, determines top-menu obstruction, advances the SPECIAL executor,
then applies popup/text/character-editor precedence. Unknown menu mode still blocks Walker even when not automated.

## SPECIAL Reference Implementation

`set_special` is the reference vertical slice:

- Concrete action: `src/Actions/Menu/SetSpecialAction.*`.
- Menu/window/execution owner: `src/Menus/SpecialAllocationHandler.*`.
- Verified FNV overlay: `src/defs/LoveTesterMenu.hpp`.
- XML reference: `/mnt/1tbssd/FalloutModding/extracts/menus/chargen/love_tester_menu.xml`.
- Adapted offset provenance: FalloutNVAccess, with `src/defs/FalloutNVAccess-license.md` preserved.

LoveTester facts for runtime `1.4.0.525`:

- Menu ID: `1074`.
- Singleton pointer: `0x11DA2C8`.
- Current page offset: `0x48`; valid pages `0..8`, normally opens on page `1` (Strength).
- Selected review SPECIAL offset: `0x4C`.
- Total points offset: `0x58`, normally `40`.
- Pages `1..7` map to actor values Strength (`5`) through Luck (`11`).
- Controls: `next_page`, `previous_page`, `increase_value`, `decrease_value`, `exit_menu`.

The visual allocation must use a reduction pass before allocation. A later attribute may need to decrease before an
earlier attribute can increase; processing increases in page order can exhaust available points and stall. Current
sequence:

```text
reduce attributes while traversing pages 1..7
-> rewind from review to Strength
-> allocate increases while traversing pages 1..7
-> verify exact values on review
-> visibly confirm and wait for the menu to leave the active stack
```

Do not infer menu-open state from the singleton pointer. The singleton remains allocated after exit and previously
caused permanent Walker lockout.

## Adding a New Action

Use one `.hpp/.cpp` module per concrete action under a clear category:

```text
src/Actions/Menu/
src/Actions/Gameplay/
src/Actions/Inventory/
src/Actions/Combat/
```

Checklist:

1. Define stable lowercase/underscore name, plaintext description, and typed object schema.
2. Put changing choices/counts in context and force state/query, not in changing action names.
3. Extend `ActionData` if the payload needs types not already supported.
4. Validate JSON shape, required values, ranges, indexes, and current selectable options.
5. Return correction messages containing the actual valid choices, such as current enemy indexes and names.
6. Capture typed values and stable state for revalidation.
7. For menu decisions, create/own an `ActionWindow` in the corresponding `src/Menus/` handler.
8. Suppress window regeneration while execution owns the menu.
9. Execute visually in bounded frame steps and verify each transition.
10. Reset local state on disconnect/load/menu invalidation.
11. Use `_MESSAGE` for major lifecycle events, `_DMESSAGE` for transport/state diagnostics, `_VMESSAGE` for per-step
    visual operations, and `_WARNING` for failures.
12. Format changed C/C++ files and run `just debug`.

## Current Limitations

- `ActionData` currently supports strict integer properties only; add typed accessors as new actions require them.
- Action success is acceptance-before-execution. Long-running execution failures must be communicated through later
  context and state recovery, not by pretending the original result represented completion.
- Menu support is incomplete. Preserve generic menu blocking and add handlers incrementally.
