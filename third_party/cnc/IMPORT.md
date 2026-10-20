# C&C source slice used by promptemon-steidium

Source: `electronicarts/CnC_Generals_Zero_Hour`.

## Network

Imported only the low-level UDP transport, LAN discovery/session support, NAT helper and related definitions. The RTS frame-lockstep command system, GameSpy, map/file transfer, RTS disconnect UI and gameplay code are intentionally not vendored.

## Graphics

Imported the W3D device integration plus the underlying WW3D2 renderer and the Westwood math/lib/debug/util support it depends on. This is kept isolated under `third_party/cnc/graphics` so promptemon-steidium can adapt rendering without inheriting Generals gameplay systems.

This code retains Electronic Arts copyright notices and is covered by the license copied to `LICENSE.md` in this directory.
