DarkPlacesRM
============

DarkPlacesRM is Akari's DarkPlaces engine fork for the RocketMinsta project that is guaranteed to be Nexuiz-compatible and based on a newer version than the standard engine.



Installing DarkPlacesRM on Linux:


open a terminal, copy & paste:

        git clone https://github.com/nexAkari/DarkPlacesRM.git

If you don't have installed "git" yet ("The program 'git' is currently not installed.") then do:

        sudo apt-get install git libsdl-dev libjpeg62-dev build-essential

That should also install most of the dependencies.

then do:

    cd DarkPlacesRM
and

    make sdl-release.

If you get an error message like this: "SDL.h no such file or directory"

try again:

    sudo apt-get install libsdl-dev libjpeg62-dev

then do:

    sudo sudo cp darkplaces-rm-sdl /usr/games/nexuiz

now, just in case, backup your Nexuiz config:

    cp ~/.nexuiz/data/config.cfg ~/.nexuiz/data/config.cfg.backup

If you have a black screen with following error message:

    You have reached this menu due to missing or unlocatable content/data
    You may consider adding 
    -basedir /path/to/nexuiz 
    to your launch commandline

execute the following commands in your terminal in this order:

    sudo wget -O /usr/games/nexuiz http://akari.thebadasschoobs.org/nexuiz.sh
    sudo chmod +x /usr/games/nexuiz
    cp ~/.nexuiz/data/config.cfg.backup ~/.nexuiz/data/config.cfg

and try to run the game again.

Now just do NOT move/delete the DarkPlacesRM folder in your home folder!


If you are using iOS, take a look at the "README.iOS".
