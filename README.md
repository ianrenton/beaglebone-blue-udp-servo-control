# Beaglebone Blue UDP Throttle/Rudder Servo Control
Receives UDP packets containing throttle and rudder demands, and sets servo outputs accordingly.

Apologies for code quality, it's been a while since I last wrote any C.

`make` does exactly what you expect. `make install` will put it in `/usr/local/bin` and create a systemd service for it to run in the background.

Based on the [Servo example](https://beagleboard.org/static/librobotcontrol/rc_test_servos_8c-example.html) from the Beaglebone Robot Control Library.
