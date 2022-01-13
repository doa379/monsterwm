TODO
----

* ~~Fix Makefile, BSD support~~
* ~~Maintain monocle state in the case when c->next = c~~
* ~~Store client size, pos~~
* ~~Restore monocles to last known size~~
* ~~Non-imposed Client placement~~
* ~~Transform client using mouse~~
* ~~Enumerate/export client list + wm positions~~
* ~~Integrate dynamic 'panel'~~
* ~~Impl. DBus notifications~~
* ~~Manipulate all clients unless strictly immutable~~
* Destroy clients on quit (Maintain clients on reload)
* ~~MM support~~
* ~~Init client state~~
* ~~Assign fixed~~
* ~~Covering algorithm~~
* ~~WM_CLASS~~
* Client name
* ~~FS/Monocle toggle~~
* ~~Simplify focus~~
* Fix focus()
* Refactor layouts
* Bad implementation, needs significant refactor (This is Release -0.0)


~~monsterwm~~ mwm
=========

mwm is a fork of monsterwm. mwm is a static tiling window manager. Short of unexpectedly ruining
any arrangement of windows through dynamic tiling. mwm more generally, also aims to be somewhat
more flexible to operate than "dwm". The mantra here is to not let the wm get in the way of 
workflows or types of flows thereof. mwm is open-ended by design. mwm has even lower resource 
requirements than "dwm". There is no Windows-esque panel native to mwm builtin. Status reports
are sent through the dbus protocol and presented as OSD notifications.

Supported to run on FreeBSD, Linux (musl libc and glibc distros).

Feature requests and bug reports are welcome.


~~→ tiny but monstrous!~~
---------------------

~~**monsterwm** is a minimal, lightweight, tiny but monstrous dynamic tiling window manager.
It will try to stay as small as possible. Currently under 700 lines with the config file included.
It provides a set of different layout modes (see below), including floating mode support.
Each virtual desktop has its own properties, unaffected by other desktops' or monitors' settings.
For [screenshots][scrot] and ramblings/updates check the [topic on ArchLinux forums][monsterwm].

  [scrot]: https://bbs.archlinux.org/viewtopic.php?id=141853
  [monsterwm]: https://bbs.archlinux.org/viewtopic.php?id=132122~~


Modes
-----

Monsterwm allows opening the new window as master or
opening the window at the bottom of the stack (attach\_aside)

---

*Common tiling mode:*

    --------------
    |        | W |
    |        |___|
    | Master |   |
    |        |___|
    |        |   |
    --------------

---

*Bottom Stack (bstack) tiling mode:*

    -------------
    |           |
    |  Master   |
    |-----------|
    | W |   |   |
    -------------

---

 *Grid tiling mode:*

    -------------
    |   |   |   |
    |---|---|---|
    |   |   |   |
    |---|---|---|
    |   |   |   |
    -------------

one can have as many windows he wants.
`GRID` layout automatically manages the rows and columns.

---

 *Monocle mode* (aka fullscreen)

    -------------
    |           |
    | no        |
    | borders!  |
    |           |
    -------------

`MONOCLE` layout presents one window at a time in fullscreen mode.
Windows have no borders on this layout to save space.
See the `monocleborders` branch to give those windows borders.

---

 *floating mode*

    -------------
    |  |        |
    |--'  .---. |
    |     |   | |
    |     |   | |
    ------`---'--

 In floating mode one can freely move and resize windows in the screen space.
 Changing desktops, adding or removing floating windows, does not affect the
 floating status of the windows. Windows will revert to their tiling mode
 position once the user selects a tiling mode.
 To enter the floating mode, either change the layout to `FLOAT`, or
 enabled it by moving or resizing a window with the mouse, the window
 is then marked as being in floating mode.

---

All shortcuts are accessible via the keyboard and the mouse, and defined in `config.h` file.

All desktops store their settings independently.

 * The window W at the top of the stack can be resized on a per desktop basis.
 * Changing a tiling mode or window size on one desktop doesn't affect the other desktops.
 * toggling the panel in one desktop does not affect the state of the panel in other desktops.


Installation
------------

You need Xlib, then,
copy `config.def.h` as `config.h`
and edit to suit your needs.
Build and install.

    $ cp config.def.h config.h
    $ $EDITOR config.h
    $ make
    # make clean install


License
-------

Licensed under MIT/X Consortium License, see [LICENSE][law] file for more copyright and license information.


Thanks (monsterwm)
------------------

 * [the suckless team](http://suckless.org/)
 * [moetunes](https://github.com/moetunes)
 * [pyknite](https://github.com/pyknite)
 * [richo4](https://github.com/richo4)
 * [Cloudef](https://github.com/Cloudef)
 * [jasonwryan](https://github.com/jasonwryan)
 * [LemonBoy](https://github.com/LemonBoy)
 * [djura-san](https://github.com/djura-san)
 * [prasinoulhs](https://github.com/prasinoulhs)
 * [mil](https://github.com/mil)
 * [dnuux](https://github.com/dnuux)
 * Matus Telgarsky
