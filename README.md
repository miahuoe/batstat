# batstat
Battery status logger and monitor.
Logs information about capacity, health, charge, voltage, temperature...
Uses a sqlite3 database.
## Requirements
- mounted /sys
- sqlite3 (for compilation)
## Features
- Run as daemon
- Save daemon's pid to file
- Redirect error messages to file
## Planned features
- Dump db to other formats
	- json
	- text file
- man page
- packaging for some popular distros
- R scripts for analysis (and to draw nice plots)
## License
- batstat is licensed under the MIT License.
- Sqlite (that is used by batstat) is a Public Domain. You can learn more about Sqlite here: https://sqlite.org/
