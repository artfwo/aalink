#! /usr/bin/env python3

import asyncio

import rtmidi
from aalink import Link

class MidiOut:
    def __init__(self):
        self._out = rtmidi.MidiOut()
        self._out.open_virtual_port("canon")

    def noteon(self, note, vel):
        self._out.send_message([0x90, note, vel])

    def noteoff(self, note):
        self._out.send_message([0x80, note, 0])

    def all_off(self):
        self._out.send_message([0xB0, 123, 0])

async def voice(link, midi, n):
    melody = [
        (48, 2),
        (50, 1),
        (52, 1),
        (55, 1),
        (57, 1),
        (55, 1),
        (52, 1),
    ]

    await link.sync(1)

    while True:
        for pitch, dur in melody:
            note = pitch + 12 * n
            midi.noteon(note, 127)

            for _ in range(dur):
                await link.sync(1 / 2 ** n)

            midi.noteoff(note)

async def main():
    midi = MidiOut()

    link = Link(120)
    link.enabled = True

    print("Connect a synth to the 'canon' MIDI port, press Ctrl+C to stop")

    try:
        async with asyncio.TaskGroup() as tasks:
            for n in range(3):
                tasks.create_task(voice(link, midi, n))
    except asyncio.CancelledError:
        midi.all_off()

asyncio.run(main())
