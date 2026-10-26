# The fear director

## Purpose

The game schedules its scares by the calendar: a row of the event table fires at its hour on its
day, whatever the players are doing. The fear director is a second clock on the host that reads the
players instead: it builds tension while the night is quiet and fires one of the game's own scares
when the moment is right, then lets the players breathe. Optionally an AI agent (the Claude API, with
the host's own key) makes those choices and leaves messages on the base computer. Every scare it may
fire is one that every player sees.

## How it works

### Tension

Once a second the host looks at the session: the clock, each player's position and rest state,
whether a player is still joining, and whether one of the game's own events is running. A tension
value from 0 to 100 climbs at a rate set by `director.intensity`, 1.8 times faster at night (20:00 to
05:59), 1.25 times at dusk, 1.4 times while two players are more than 40 m apart, and slowly while
someone sleeps or right after a scare. When it crosses the threshold the director fires a scare,
drops the tension by 35 to 65 depending on the scare's power, rolls a new threshold between 55 and
90 and holds a breather of one and a half to three minutes (shorter at higher intensity). It holds
while a player joins, while anyone is in bed (`director.respect_sleep`; one of the game's events
wakes a sleeper) and while one of the game's own events runs, so it never stacks on a story beat
(`coop/fear/fear_director`). That last hold lasts three minutes from the rise of the game's event count:
some event actors keep their registration for the whole session, and a count that never falls is taken
as stuck (logged once, with the classes holding it) rather than silencing the night.

### What it may fire

The catalog (`coop/fear/fear_catalog`) holds only scares an existing lane already carries to
every peer, each row naming its lane:

| Kind | Rows | Who sees it, and how |
|---|---|---|
| creature | `ventCrawler`, `tentacleBalls`, `trySpawnInsomniac` (night) | the host runs the brain, the npc lane mirrors it |
| trap | `toeStab`, `falseEnter`, `mann`, `vent`, `fakeGrays` | the arm is replayed on every peer; each player who walks in gets it |
| device | `lightswitch` (night), `console`, `keypadGuess` | the light, terminal and keypad lanes |
| atmosphere | `spawnFog` | the weather lane mirrors the host's fog |
| spectacle | `solar` | replayed on every peer |
| deadly, off by default | `graysforest`, `eggvasion` | the npc and world-actor lanes |

The rules pick by weight: creatures at night and when the players huddle together, traps and devices
when they split up, never the same kind twice in a row if anything else is allowed, stronger scares the
further the tension has run past its threshold. Each row has its own cooldown.

A trap re-armed in the same session is replayed again on a client (`event_fire_policy`'s re-arm
list); every other eventer row keeps the one-shot replay it had.

### The AI agent

With `director.ai` on and a key in `director.ai_key` (or the `ANTHROPIC_API_KEY` environment
variable), the host asks the agent every `director.ai_period_s` seconds once the tension passes 25,
and at once when a peak comes. The request carries the clock, the players' nicknames, rest states and
distances, the tension, the recent scares and the rows allowed right now; the answer is one JSON
decision: fire a named row or wait, an optional email, and a sentence about its plan for the host's
pane (`coop/fear/fear_ai`). The rules hold the agent to themselves: it can only name a row
allowed now, never below a tension of 35 or during a breather, and a peak it keeps waiting on fires by
the rules after 90 seconds. Three failed requests in a row hand the night to the rules for five
minutes. An email goes through the game's own `addEmail` on the host, at most one every ten minutes,
and the email lane shows it to everyone. The request is made only from the host, only with the host's
key, to `api.anthropic.com` (`coop/fear/fear_http`); the key is a credential row and never
reaches the log or another peer.

### Sleep

The night speeds up only when everyone is in bed. A player who lies down while another is still up
rests at the game's native sleep factor on their own body with the world at normal speed, the host as
well as a client. When the last one lies down the host's world runs the game's own speed-up and every
client's clock follows; when anyone gets up, or a player joins, it slows back down
(`coop/player/sleep_rest`). Each peer writes its own chat lines from the rest table it already holds:
who went to bed and how many are down, everyone asleep, someone woke (`coop/player/sleep_sync`).

### Scare characters

The mannequin scare and the jump scare that senses a pawn are characters a trigger spawns for the
player who walked into it. They join the eyer in the owner-entity lane: the spawning peer keeps its
own, and the others see a parked mirror where it stands (`coop/creatures/owner_entity_sync`).

## Who owns what

| State | Owner | Shape |
|---|---|---|
| tension, threshold, cooldowns, the agent's requests | the host | local; never on the wire |
| a fired scare's outcome | the lane its row names | as for any host fire of that row |
| the shared night | the host | its own world dilation; clients follow its clock |
| a scare character | the peer it spawned on | the owner-entity lane |

## Wire messages

None of its own. A director fire is a host fire like any other (`EventFire` and the lanes the rows
name); the sleep lines are derived from `SleepState`. The build number is 9000 plus the upstream
build the fork is based on, so its lobbies do not pair with official ones.

## Late join

A joiner holds the director (and the shared night) until its world is ready. Traps armed before it
joined are not re-armed for it until the next fire of that row.

## Known limits

| Limit | Evidence |
|---|---|
| Whether a re-fired trap re-arms on the host depends on the trigger the game placed; one that destroys itself after firing stays spent | `[?]` the eventer's trigger bodies are not read here |
| `trySpawnInsomniac` can be declined by the game's own gates; the director still counts it as fired | `[RD]` the dev event board's note on the verb |
| The scare characters' mirrors run their `BeginPlay` on the viewing peer before they are parked | `[?]` the owner-entity lane's shape; to be measured |
| The AI agent's email sender is the third entry of the game's email-character enum | `[?]` read from the enum's display-name order |

## Code map

| Concept | Files |
|---|---|
| tension, holds, choice, the pane's view | `coop/fear/fear_director` |
| the catalog | `coop/fear/fear_catalog` |
| the AI agent and its request | `coop/fear/fear_ai`, `coop/fear/fear_http` |
| the ambient verbs | `ue_wrap/world/ambient_verbs` |
| the pane | `ui/fear_pane` |
| the shared night | `coop/player/sleep_rest`, `coop/player/sleep_sync` |
| the re-armed traps | `coop/world/event_fire_policy`, `coop/world/event_fire_sync` |
