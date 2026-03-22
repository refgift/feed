<img width="819" height="661" alt="Screenshot From 2026-03-10 13-33-26" src="https://github.com/user-attachments/assets/cab6fd79-9809-4329-87da-a3a9ed1e69ab" />

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
- Code blocks in responses are automatically extracted and saved to files.
- Output is formatted with uniform spacing (like fmt -u) for better readability.

## Advanced Usage
- **Sending file contents**: `feed "$(cat README.md)"` to include file content in the prompt.
- **Redirecting output**: `feed "How to shower without soap" > shower_advice.txt` to save response to a file.
- **Automatic code saving**: `feed "Write a hello world in C"` automatically extracts code from ```c ... ``` blocks and saves to files (e.g., food.c).
- **Combining with shell**: Pipe or script with feed for automation, e.g., `echo "Analyze this log" | xargs feed > analysis.txt`.
## Dependencies
- uses the curl utility found in every Linux repository, if not already installed.
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
- FEED_USER (optional): Name to use in prompts. Defaults to "Anonymous" if unset.
- FEED_CONTEXT (optional): Context/system prompt for the AI. Defaults to "You are Grok, a helpful and maximally truthful AI built by xAI." if unset. Can be customized (e.g., for programming focus).
### Error Handling
- If "no content in response," try using just your first name in FEED_USER or unset it for anonymous mode.
## Build
- A C compiler is necessary, gcc or clang are the names in the Linux world.
### Build Commands
- cd into the feed directory
- gcc feed.c -o feed
#### Advanced Build - uses chibicc C compiler by default others can be used.
- make
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
