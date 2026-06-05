<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/cda51768-fdf8-440c-a3d6-0353708ed9cd" />


# feed - a terminal based prompter for use with xAI's responses API.

## Limitation
- Works with xAI's responses API (https://api.x.ai/v1/responses).

## Usage
Companies like xAi and OpenAI sell access to their AI systems with API keys and payment by tokens used.
They have models, like grok-1 which is different from opus. Model names and api key are the gate and
feed is a user interface of the simplest form, a command line command that takes a prompt to send to an AI and wait for the AI response which is formatted and printed when it arrives. Then feed is done. The API
that is used is xAI's responses API.
The interface to the AI server is stateless. Nothing happens outside of the prompt and response unless the
client, like feed, keeps the "session" alive. You can use the command history and edit the prompt you previously
sent and keep it going for a long time that way.
feed can be called from shell programs. There might be utility to periodical use of feed to get a system analysis
by building a prompt with system data collections. You could use ls -lotr to find folders and tar files and make a prompt
of that and ask for a shell script that tar compresses the folders, then meta-tar the tarfiles.
- You can redirect the feed output to a file.
- Use --debug or -d for debugging: prints API URL, JSON payload, and raw response.
- Use -t to run test suite (21 tests covering JSON conformance, API response parsing, escaping, and formatting; recommended first step for new users).
- Use --stateless for single-shot mode (no REPL, no session).
- Use --stateful for REPL mode (interactive with in-memory session caching via previous_response_id).
- In REPL: prompt shows current model (e.g. `grok-beta> `). Use `/model <name>` to change model and clear session. Type `quit`, `exit`, or `bye` (or Ctrl-C) to end.
- Code blocks in responses are automatically extracted and saved to files.
- Output is formatted with uniform spacing (like fmt -u) for better readability. Markdown in prompt/response (headers, lists, quotes, code blocks) is prettied with dynamic indents.

## Advanced Usage
- **Sending file contents**: `feed "$(cat README.md)"` to include file content in the prompt.
- **Redirecting output**: `feed "How to shower without soap" > shower_advice.txt` to save response to a file.
- **Automatic code saving**: `feed "Write a hello world in C"` automatically extracts code from ```c ... ``` blocks and saves to files (e.g., food.c).
- **Stateless mode**: `feed --stateless "One-off question"` to prevent response persistence.
- **Combining with shell**: Pipe or script with feed for automation, e.g., `echo "Analyze this log" | xargs feed > analysis.txt`.

## JSON Conformance
The built-in JSON parser conforms to ECMA-404, 1st Edition (October 2013). The reference specification is included in `specs/ECMA-404_1st_edition_october_2013.pdf`.
- **Numbers** (Section 8): Superfluous leading zeros are rejected (e.g., `07` is invalid, `0.7` and `0` are valid).
- **Strings** (Section 9): All escape sequences are supported: `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`, and `\uXXXX` hex escapes.
- **Control characters**: U+0000 through U+001F are properly escaped as `\uXXXX` when producing JSON output.
- **UTF-16 surrogate pairs**: Non-BMP characters encoded as surrogate pairs (e.g., `\uD834\uDD1E`) are decoded to UTF-8.

## Responses API Conformance
The response parser uses the built-in JSON parser to navigate the API response tree structure.
- **Request**: Sends `model`, `input`, `store`, and optionally `instructions` (for system prompts via FEED_CONTEXT).
- **Response parsing**: Navigates `output[].content[]` to find items with `type: "output_text"` and extracts the `text` field. Skips reasoning and other non-message output items.
- **Error handling**: Checks the parsed `error` object; extracts `error.message` if present.
- **Status checking**: Warns if `status` is not `"completed"` and reports `incomplete_details.reason` if available.

## Dependencies
- `curl` (for API calls)
- GNU readline (`-lreadline`) for REPL mode (history, editing, multi-line support)
## Configuration
### Shell Environment Variables
- To set these, from your terminal session you type export FEED_KEY=$XAI_API_KEY, for example,  and press return.
- You can place the export of your AI session data in the .bashrc file that will set them for you.
- Please be aware that these data are expensive and should be protected by the system with logout and
  automatic logout and screen lock. The Linux system has good system security, in my opinion.
### These are the variables that feed uses.
- FEED_URL (required): https://api.x.ai/v1/responses
- FEED_KEY (required): your xAI API key (starts with `xai-...`)
- FEED_MODEL (required): grok-beta (or grok-2-latest, etc.)
- FEED_CONTEXT (optional): system prompt sent via the API `instructions` field
### Command-Line Options
- -t: Run test suite (21 tests: JSON parsing, leading-zero rejection, control character escaping, API response parsing, text formatting) and exit.
- --stateless: Sets "store":false in the API request for stateless mode (no response persistence).
- --stateful: Explicitly sets "store":true for stateful mode (default behavior). --stateless and --stateful are mutually exclusive.
- --ask-name: Interactively prompt for filename when saving code blocks.

## Build
- A C compiler is necessary, gcc or clang are the names in the Linux world.
### Build Commands
- cd into the feed directory
- make
- Or manually: `gcc -D_GNU_SOURCE -std=c99 -Wall -o feed feed.c`
#### Advanced Build - uses chibicc C compiler by default others can be used.
- Edit the CC line in Makefile to switch compilers.
#### Uses the splint command, has to be installed
- make lint
### Build Test
- ./feed
- Without arguments it will tell you to give it an argument.
- ./feed --debug "test prompt" (requires env vars set) to see debug output.
## System Configuration
- Placing feed into the system PATH will allow it to be used outside of it's build directory.
- sudo cp feed /usr/local/bin will allow you to depense with the ./
- mkdir ~/bin then cp feed ~/bin which can be done without security other than your authority.
### Manpage Installation
- To enable `man feed`, install the manpage:
  - sudo mkdir -p /usr/local/man/man1
  - sudo cp feed.1 /usr/local/man/man1/
  - sudo mandb
- For local installation without sudo:
  - mkdir -p ~/man/man1
  - cp feed.1 ~/man/man1/
  - export MANPATH="$HOME/man:$MANPATH"
  - mandb ~/man
  - Add the export to ~/.bashrc for permanence.
