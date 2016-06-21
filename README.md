huipfui
=======

This is the HUman Interface Pipe Framework Under Implementation.

Overview
--------

This project was started when I arrived in a holidays flat. I had a
Raspi in my bag, which I intended to attach to the TV set in order to
run the multimedia software [Kodi](http://kodi.tv/). But then I noticed
I had forgotten to take a keyboard or mouse with me. So what could I do?
At least, I had the keyboard of the notebook. Could this keyboard be
used to control the Raspi?

There are readily available solutions
[x2x](http://github.com/dottedmag/x2x) and
[synergy](http://synergy-project.org/), that allow to control another
computer over the network. But these require an X server on Linux and on
Raspis Kodi runs directly on the frame buffer. So these solutions were
of no use for me. I also refuted remote desktop tools as pushing video
through the network seemed to be too much overhead. If your keyboard and
mouse are USB devices, you also might consider
[usbip](http://usbip.sourceforge.net/), which can connect USB devices
over IP networks. But my notebook keyboard was not attached via USB.

However, linux seems already well prepared for replicating user input
devices. A a matter of fact, you can create virtual input devices with
the uinput driver, which is usually found as /dev/uinput or
/dev/input/uinput depending on distribution. So the simple idea is to
read input events from the real hardware on the notebook and feed it to
the uinput on the Raspi.

Thus, the solution appears to be simple. We only need a program that can
read the input events from the real hardware devices and another one to
write them to /dev/uinput. The program to read the events is called
hid2out. It sends representations of the events to standard output.
These can then be fed to other program named in2hid

In the configuration file you can select the input devices by listing
their paths. You also can configure for each event, the action it should
trigger. So apart from the default of writing its representation to the
standard output, you can determine, that a modified event should be
written, that the event should be ignored, that a process should be
startd or that hid2out should be terminated. events

As in2hid creates an input device in Linux which cannot be distinguished
from real hardware input devices, this project can be used independent
of any desktop or GUI environment. In contrast to other solutions you
don’t need X11. You can use it with KDE or Gnome and then switch to text
console and still continue using it. Of course, it works with software
that used the frame buffer directly like Kodi on Raspberry Pi.

Building
--------

You’ve everything you need, if you have the usual Linux software
development tools installed and the library libconfig with its
development files. On Debian it should suffice to enter the following
line to get all necessary packages:

    apt-get install build-essential libconfig-dev

In the directory cloned from the repository of this project simply run
the following commnand:

    make

Installing
----------

Place the two executables `hid2out` and `in2hid` built in the previous
section into a directory which makes calling them most convenient for
you. On most distribution `/usr/local/bin` is a nice place.

Make sure that you can access the input devices that you want to be read
by hid2out. Input devices are usually accessible by the group called
`input`. So the user starting hid2out has to be (made) a member of that
group. Suppose her user name is Alice. Then you may execute in your
shell:

    adduser alice input

Before actually running hid2out a configuration file is needed. How to
write one is explained in the next section.

The user running in2hid needs the uinput device node with write access
to it. The node will be created automatically in `/dev/uinput`
resp. `/dev/input/uinput`, when the module uinput is loaded. So you may
want to add the module name `uinput` to the list of modules to be loaded
at boot time. On current Debian system you would add a file to the
directory `/etc/modules-load.d` containing just one line:

    uinput

In most other cases you can add the line to the file `/etc/modules`.

Most often you may already find a device node `/dev/uinput` even before
the module is loaded. Then your system will probably load the module
automatically by on demand loading, when accessing `/dev/uinput` for the
first time. But we discourage to make use of on demand loading as this
would require to configure the permissions twice, once for the time when
the node is created and a second time for when the module is loaded. If
these settings differ, you will note, that `/dev/uinput` changes
permissions at the time it is accessed the first time.

For security reasons you may choose to have only one particular user
allowed to access uinput. Let’s call him in2hid and create his account:

    adduser uinput --disabled-password --shell /usr/local/bin/in2hid

Now we need to set the permissions of `/dev/uinput`. The udev service
takes care of all nodes, when they are created by loading their modules.
A rule that makes it set permission to out needs could be:

    KERNEL == "uinput", OWNER = "uinput", GROUP = "uinput", MODE = "0660"

(If you don’t need access for the group remove the `GROUP` equation and
adjust the mode.) This line is put into a file, where udev will find it,
for example `/etc/udev/rules.d/80-uinput.rules`.

Configuration
-------------

The default configuration file is looked for in
`$HOME/.config/hid2out.conf`. But you can give any other desired path as
first argument on the command line.

We use libconfig to parse the file so for basic syntax please refer to
the documentation of the library.

The following settings have to be contained in the file:

<span>\
**input\_devices**</span>: A list of strings is expected. Each item in
the list has to be a path to an input device node. With this list you
select the hardware that is watched by hid2out. <span>\

**eventmaps**</span>: A list of settings is expected. Each setting
associates a name to an event map, which defines the actions that are
triggered for particular input events. At least a map named `main` must
be present. This is the map that is in effect initially. (Later you can
switch to another map.)

An input map is a list of rules. A rule is a list of a pattern and an
action. For each input event the rules are tried in the order they occur
in the map. The pattern is matched against the channel event and if it
succeeds, the action is executed. A channel event consists of four
attributes:

-   channel

-   type

-   code

-   value

The latter three correspond to the slots with the same name of the
struct `unput_event` in `linux/input.h`. The first one identifies the
input device from which the event has been read. They are numbered
according to the paths in the input\_devices setting. The first element
is associated with channel number 0, the second with channel number 1,
and so on. The channel event matches with the pattern if all of its
attributes are within the ranges.

Ranges are represented by lists of length 0 to 2 containing integers and
empty lists. The first element indicates the minimum, the second one the
maximum, provided that they are not missing. A bound of the range is
also considered missing if it is not integers but an empty list.

You may use evtest to help you figure out how the events triggered by
your input hardware are represented internally.

An action is a list with an action name, a string, as its first element
followed by arguments or it is an empty list.

These are the actions that are understood:

<span>\
**exit**</span> (no arguments): Terminate hid2out. <span>\
**switch**</span> with an event map name as argument: Switch the current
event map to the named one. <span>\
**send**</span> followed by 4 integers/empty lists: Write a
representation of the channel event to the standard output. The four
arguments replace the attributes of the original channel event, in case
they are integers and not missing. Empty lists are considered missing
arguments. <span>\
**exec**</span> followed by a string: Lauch the process indicated by the
string, i.e. the linux standard library function system is called with
string as its argument. <span>\
**An empty list**</span>: Do nothing, just ignore the event.

The default action is send. So if you only want an event to be passed to
the standard output you don’t have to give a rule for that.

You should at least define a rule with the action “exit”. Otherwise, you
may not be able to terminate the program. Note, that Ctrl-C may not
work, if it is grabbed by hid2out and your shell will not see it.

Let’s have a look at an example configuration file

    input_devices =
              ( "/dev/input/by-id/manufacturer-a-event-kbd",
                "/dev/input/by-id/manufacturer-b-event-kbd")

    eventmaps = { main = (
                 #if division key on num pad of first keyboard released,
             # stop and go back to normal
                 ((1, 1, 98, 0), ("exit")),
                 #but when pressed, ignore
                 ((1, 1, 98, 1), ()),
                 #if + on num pad released, sitch on screen of raspi
                 (((), 1, 78, 0), ("exec", "xrandr --output VGA1 --off")),
                 #when pressed, ignore
                 (((), 1, 78, 1), ()),
                 #if - on num pad released, switch off screen of raspi
                 (((), 1, 74, 0), ("exec", "xrandr --output VGA1 --auto")),
                 #press - on num pad 
                 (((), 1, 74, 1), ()),
                 #when calculator key is released, switch to the other eventmap below
                 (((), 1, 140, 0), ("switch", "calc")),
                 #ignore again
                 (((), 1, 140, 1), ())
                 ),

                 #With the calculator key the special functions of the other keys
                 #can be toggled on an off.
              calc = (
                 #release calculator key
                 (((), 1, 140, 0), ("switch", "main")),
                 #press calculator key
                 (((), 1, 140, 1), ())
                 )   
                 }  

Here two input devices are grabbed by hid2out. You can stop the program
by pressing the division sign key on the numeric key pad. With plus and
minus key, the VGA output is activated resp. deactivated with the help
of xrandr. The calculator key can be used to deactivate the special
functionality of the other keys. So their events will be forwarded to
standard output. Only the calculator key is now special and is used to
switch back to the initial event map.

Example Use Scenarios
---------------------

As already illustrated in the introduction, one example use case is
replicating the input devices on remote machines. For example just use
the keyboard attached to one computer as input device for another
machine. You can use the big keyboard attached to your desktop with your
laptop without having to unplug and plug every time you change from
desktop to laptop.

The events are represented in a way that is human readable, or let’s
better put it nerd readable. So you could be able to put your own tools
in between to modify the event stream or have useful things being
triggered by certain events.

We avoided the pitfalls of implementing a secure protocol. So we better
rely on ssh for that and you could connect your local input devices to a
remote system entering this line:

    hid2out | ssh <username>@<host> in2hid

Or if you defined the user `uinput` as above with in2hid as shell, you
can use this command line:

    hid2out | ssh uinput@<host>

You may also want to use huipfui locally if you need to intercept
keyboard or mouse input.

-   Define keyboard shortcuts. Desktop environments already allow you to
    define your keyboard shortcuts. But with huipfui you get some
    advantages:

    -   Shortcuts also work in text console mode

    -   You only need to define them once and not for each desktop
        environment you are using.

    -   You can define shortcuts for keys your desktop environment is
        not able to handle, like for example number pad keys in KDE

    -   You can even define shortcuts for mouse buttons.

    -   You can even define shortcuts for switches you didn’t know they
        existed. Did you know that some headphone and microphone sockets
        are linked to linux input device nodes? So you can have useful
        things triggered when plugging in a headphone or a microphone.

    -   You need not reconfigure your shortcuts, if your desktop
        environment increments its version number.

-   Escape applications that grab the human user interface devices Some
    applications running directly on frame buffer grab all input
    devices. Then the key combination for switching to another virtual
    terminal is blocked and you cannot terminate the application if it
    is hung. With huipfui you can grab the input before starting the
    application and define a key to kill the application.

-   Temporary work around a broken keyboard.

For local use you could use the following command line:

     hid2out | in2hid

License
-------

The huipfui project is open-sourced software licensed under the [GNU
General Public License v 3.0](https://opensource.org/licenses/GPL-3.0)
