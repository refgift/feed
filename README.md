# feed - a terminal based prompter for use with AI API services using the OpenAI REST protocol.

## Limitation
- Currently only tested with xAI grok API system. A fix is coming.

## Usage
Companies like xAi, and OpenAI sell access to their AI systems with API keys and payment by tokens used.
They have models, like grok-code-fast which is different from opus. Model names and api key are the gate and 
feed is a user interface of the simplest form, a command line command that takes a prompt to send to an AI
and wait for the AI response which is formatted and printed when it arrives. Then feed is done. The API
that is used is based on the OpenAi which designed it and it standardized. 
The interface to the AI server is stateless. Nothing happens outside of the prompt and response unless the
client, like feed, keeps the "session" alive. You can use the command history and edit the prompt you previously
sent and keep it going for a long time that way.
feed can be called from the shell programs. There might be utility to periodical use of feed to get a system analysis
by building a prompt with system data collections. You could use ls -lotr to find folders and tar files and make a prompt 
of that and ask for a shell script that tar compresses the folders, then meta-tar the tarfiles. 
- You can redirect the feed output to a file.

## Dependencies
- uses the curl utility found in every Linux repository, if not already installed.

## Configuration

### Shell Environment Variables
- To set these, from your terminal session you type export FEED_KEY=$XAI_API_KEY, for example,  and press return.
- You can place the export of your AI session data in the .bashrc file that will set them for you.
- Please be aware that these data are expensive and should be protectedby the system with logout and
  automatic logout and screen lock. The Linux system has good system security, in my opinion.

### These are the  variables that feed uses. 
- If they are unset, feed will tell you and quit.
- FEED_URL is the api url for your AI service
- FEED_KEY is the api key for your AI service
- FEED_MODEL is the model name you can use with your AI service
- optional:  FEED_USER is the name you want to use with your AI service

## Build
- A C compiler is necessary, gcc or clang are the names in the Linux world.

### Build Commands
- cd into the feed directory
- gcc feed.c -o feed 

### Build Test
- ./feed 
- Without arguments it will tell you to give it an argument.

## System Configuration
- Placing feed into the system PATH will allow it to be used outside of it's build directory.
- sudo cp feed /usr/local/bin will allow you to depense with the ./ 
- mkdir ~/bin then cp feed ~/bin which can be done without security other than your authority.


<img width="896" height="1072" alt="Screenshot From 2026-03-08 20-21-10" src="https://github.com/user-attachments/assets/55e13e72-10e6-46f1-a00a-cc5a50715090" />