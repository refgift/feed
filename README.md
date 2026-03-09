# feed - a terminal based prompter for use with LLM API services using the REST protocol.

## Dependencies
- uses the curl utility found in every Linux repository, if not already installed.

## Configuration
### Shell Environment Variables
- To set these, from your terminal session you type export FEED_KEY=xxxx and press return.
### These are the two variables that feed uses. If they are unset, feed will tell you and quit.
- FEED_KEY is the api key for your AI service
- FEED_MODEL is the model name you can use with your service

## Build
- A C compiler is necessary.
### Build Commands
- cd into the feed directory
- gcc feed.c -o feed 
### Build Test
- ./feed 
- Without arguments it will tell you to give it an argument.


