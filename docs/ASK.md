# Ask

Stuck in a game? Ask tells you what to do next. Open the in-game guide and choose
**What now?**, or press X on the controller (□ on PlayStation, Y on Nintendo, X on the
keyboard). Omakade takes a picture of the paused game, asks your AI agent where you are
and what comes next, and shows the answer on the card. Answers stay spoiler-free: where to
go and how to get past what is in front of you, never what happens later.

With Claude Code, **Look it up** then asks again with a web search, so the agent can check
its first answer against a guide and add the detail you need. The answer names the guide it
used.

Turn it on in **Settings → Ask**. It is off until you do, because each question sends a
picture of your game to your agent's company.

## Your agent

Ask uses the agent you chose in Omarchy's menu under **Setup → Defaults → Agent**, with
your own account and its usage limits. Ask works with:

- **Codex.** Ask runs `codex exec` in its read-only sandbox, with low reasoning effort.
  Everything Codex could use to read your files is turned off: its shell, code-mode host,
  plugins, image viewer, sub-agents and skills, plus MCP servers, apps, the browser and
  memories. Codex answers from what it knows, without Look it up: its web search needs the
  code-mode host, which can also run code that reads any of your files.
- **Claude Code.** Ask runs `claude -p` with the picture in the question and no tools at
  all, and no MCP servers, hooks or settings. Look it up gives it web search and lets it
  open the pages it finds, nothing else.

Other agents Omarchy offers can't be used yet, and Settings says so when one of them is
your default. Changing the default in Omarchy takes effect with the next question.

Every question runs in an empty private folder and leaves no session in the agent's
history. These limits were checked against Codex 0.164 and Claude Code 2.1; a tool a later
version adds may need turning off too.

## How long it takes

A first answer usually takes about 5 seconds with Claude Code and 5 to 15 with Codex,
depending on how busy its service is. The card counts the seconds while it waits. Look it
up searches first, so it takes 10 to 30 seconds. After a minute without an answer Ask
gives up and says so.

One question runs at a time. The answer stays on the card for the rest of the session, so
you can resume, play, and open the guide again to read it. **Ask again** takes a new
picture and starts over. Another game starts from nothing.

## When it can't answer

The card says why in place of an answer: the agent isn't installed, its sign-in has
expired, you've reached its usage limit, or it didn't answer in time. **Try again** asks
again once you have fixed it.

## What is sent

The game's name, its platform when Omakade knows it, and one picture of the display the
game is on, scaled to about 960 pixels wide. Anything shown over the game on that display
is in the picture; the guide's card is not. Nothing else of yours is sent. The picture goes
to the company behind your agent (OpenAI for Codex, Anthropic for Claude Code) under your
account, as anything you ask the agent does.

Omakade keeps the picture in a private folder in memory
(`$XDG_RUNTIME_DIR/omakade/ask/`) until your next question or game, and deletes it then.
Answers are not saved.
