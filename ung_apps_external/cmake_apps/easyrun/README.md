# EasyRun (`er`)


    $ er -h
    Usage: er [-l lock-file] [-shv] -- <program and options>

    Where:
      -v               Print version
      -b               Run in background
      -h               This help message
      -l <lock-file>   Is the path the a lock file for IO sync
      -s               Flag to skip the stdin pipe



# Build it, install run

    $ mkdir build
    $ cd build
    $ cmake ..
    $ make
    $ sudo make install


