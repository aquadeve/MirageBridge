local vr = require("vr")

assert(vr.connect("local"), "MirageBridge runtime is not available")

while vr.running() do
    local frame = vr.waitFrame(1000000000)
    if frame and frame.shouldRender then
        local pose = vr.getPose()
        if pose then
            print(
                string.format(
                    "frame=%d pos=(%.3f, %.3f, %.3f)",
                    pose.frameId,
                    pose.position.x,
                    pose.position.y,
                    pose.position.z
                )
            )
        end
    end

    local event = vr.pollEvents()
    while event do
        print(string.format("event type=%d value=%d", event.type, event.value))
        event = vr.pollEvents()
    end
end
