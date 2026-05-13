# Luau API

The Luau layer is intentionally small and synchronous at the lowest level. Hosts can wrap it with coroutines, promises, or engine-specific task schedulers.

## Loading

Build output:

```text
termux/build/sdk/vr.so
```

Example:

```luau
local vr = require("vr")
assert(vr.connect("local"))
```

## Core Functions

### `vr.connect(endpoint: string?): boolean`

Connects to the local MirageBridge runtime. Supported endpoints today:

- `"local"`
- `"localhost"`
- `"shm"`

### `vr.disconnect()`

Closes runtime readers/writers.

### `vr.running(): boolean`

Returns true while the runtime is connected.

### `vr.waitFrame(timeoutNs: number?): table?`

Waits for a pose packet and returns:

```luau
{
    frameId = number,
    predictedDisplayNs = number,
    displayPeriodNs = number,
    shouldRender = boolean,
}
```

### `vr.getPose(): table?`

Returns:

```luau
{
    frameId = number,
    timestampNs = number,
    predictedDisplayNs = number,
    position = { x = number, y = number, z = number },
    rotation = { x = number, y = number, z = number, w = number },
    linearVelocity = { x = number, y = number, z = number },
    angularVelocity = { x = number, y = number, z = number },
}
```

### `vr.getController(index: number): table?`

Returns:

```luau
{
    id = number,
    connected = boolean,
    buttons = number,
    trigger = number,
    position = { x = number, y = number, z = number },
    rotation = { x = number, y = number, z = number, w = number },
}
```

### `vr.pollEvents(): table?`

Returns the next event or nil:

```luau
{
    id = number,
    type = number,
    code = number,
    value = number,
    timestampNs = number,
}
```

### `vr.metrics(): table?`

Returns counters:

```luau
{
    posePacketsRead = number,
    framePacketsRead = number,
    framesSubmitted = number,
    audioPacketsSubmitted = number,
    droppedEvents = number,
}
```

## Coroutine Pattern

```luau
local vr = require("vr")
assert(vr.connect("local"))

local function frameLoop()
    while vr.running() do
        local frame = vr.waitFrame(1000000000)
        if frame and frame.shouldRender then
            coroutine.yield(vr.getPose(), frame)
        end
    end
end

local co = coroutine.create(frameLoop)
while coroutine.status(co) ~= "dead" do
    local ok, pose, frame = coroutine.resume(co)
    if ok and pose then
        app.camera:setTransform(pose.position, pose.rotation)
    end
end
```

## Planned Luau Surface

The native module currently exposes pose, controller, events, timing, and metrics. The stable target API is:

```luau
local vr = require("vr")

vr.connect("local")
local headset = vr.getHeadset()
local controller = vr.getController(1)

while vr.running() do
    local timing = vr.waitFrame()
    local pose = headset:getPose()
    local frame = app.renderer:renderStereo()
    vr.submitFrame(frame)
    vr.pollEvents()
end
```

That object-oriented wrapper can be implemented in pure Luau on top of the current native functions.
