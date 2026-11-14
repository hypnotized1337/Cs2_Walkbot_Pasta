-- PASTA Lua API test.
-- Every check compares a real return value/type against an expected value.
-- Writes to config and game input are disabled until the opt-in toggle is on.

local API_VERSION = "1.0.0"
local CONFIG_PATH = "misc.trigger_debug_overlay"
local STORAGE_KEY = "pasta_api_test_roundtrip"
local MISSING_STORAGE_KEY = "pasta_api_test_missing"
local INPUT_KEY = 222 -- VK_OEM_7, a key the script runtime explicitly allows.
local PROBE_TEXTURE = "pasta_api_test_probe.png"

local event_names = {
    "load", "tick", "unload", "config_changed",
    "frame", "game_connected", "game_disconnected", "map_changed",
    "player_joined", "player_left", "player_hurt", "player_killed",
    "weapon_changed", "projectile_created", "projectile_detonated", "projectile_removed",
    "bomb_planted", "bomb_defuse_started", "bomb_defuse_stopped", "bomb_defused",
    "bomb_resolved", "confirmed_hit",
}

local api_names = {
    "vesta.log",
    "vesta.api.major", "vesta.api.minor", "vesta.api.script_dir", "vesta.api.data_dir",
    "vesta.events.on", "vesta.events.off",
    "vesta.storage.set", "vesta.storage.get", "vesta.storage.remove",
    "vesta.game.snapshot", "vesta.game.radar_snapshot", "vesta.game.world_to_screen",
    "vesta.game.trace_ray", "vesta.game.bomb_damage", "vesta.game.predict_grenade",
    "vesta.game.radar_overview", "vesta.game.penetration_damage",
    "vesta.config.get", "vesta.config.set", "vesta.config.patch",
    "vesta.config.snapshot", "vesta.config.schema",
    "vesta.ui.text", "vesta.ui.button", "vesta.ui.toggle", "vesta.ui.slider",
    "vesta.ui.select", "vesta.ui.get", "vesta.ui.input", "vesta.ui.color",
    "vesta.ui.keybind", "vesta.ui.separator", "vesta.ui.consume",
    "vesta.draw.line", "vesta.draw.rect", "vesta.draw.filled_rect",
    "vesta.draw.circle", "vesta.draw.filled_circle", "vesta.draw.text",
    "vesta.draw.world_line", "vesta.draw.world_text", "vesta.draw.load_texture",
    "vesta.draw.image", "vesta.draw.panel",
    "vesta.input.is_down", "vesta.input.binding", "vesta.input.key",
    "vesta.input.tap", "vesta.input.mouse_move", "vesta.input.mouse_button",
    "vesta.assets.weapon_font",
    "vesta.helpers.start", "vesta.helpers.copy_text",
}

local status_by_name = {}
local event_counts = {}
local event_tokens = {}
local event_errors = {}
local probe_texture = nil
local game_snapshot_value = nil
local storage_value = nil
local log_error = nil
local draw_failures = {}
local results_draw_failed = false
local summary_text = "Not run yet"
local pending_config = nil
local consume_clicked = false
local start_config_write_checks
local event_summary

local off_test = {
    token = nil,
    count = 0,
    phase = 0,
    seen = 0,
    baseline = 0,
    result = nil,
}

local function short_value(value)
    if type(value) == "table" then
        local parts = {}
        for key, item in pairs(value) do
            parts[#parts + 1] = tostring(key) .. "=" .. tostring(item)
            if #parts >= 4 then
                parts[#parts + 1] = "..."
                break
            end
        end
        return "{" .. table.concat(parts, ", ") .. "}"
    end
    return tostring(value)
end

local function is_finite(value)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

local function is_integer(value)
    return is_finite(value) and value == math.floor(value)
end

local function vec3_ok(value)
    return type(value) == "table" and is_finite(value.x) and is_finite(value.y) and is_finite(value.z)
end

local function vec2_ok(value)
    return type(value) == "table" and is_finite(value.x) and is_finite(value.y)
end

local function deep_equal(a, b)
    if type(a) ~= type(b) then
        return false
    end
    if type(a) ~= "table" then
        return a == b
    end
    local count_a = 0
    for key, item in pairs(a) do
        count_a = count_a + 1
        if not deep_equal(item, b[key]) then
            return false
        end
    end
    local count_b = 0
    for _ in pairs(b) do
        count_b = count_b + 1
    end
    return count_a == count_b
end

local function record(status, name, detail)
    detail = detail == nil and "" or tostring(detail)
    status_by_name[name] = {status = status, detail = detail}
    local line = status .. " " .. name
    if detail ~= "" then
        line = line .. " - " .. detail
    end
    if #line > 1000 then
        line = line:sub(1, 997) .. "..."
    end
    local ok, error_text = pcall(vesta.log, line)
    if not ok then
        log_error = tostring(error_text)
    end
end

local function check(name, fn)
    local ok, verdict, detail = pcall(fn)
    if not ok then
        record("FAIL", name, "error: " .. tostring(verdict))
        return
    end
    if verdict == true then
        record("PASS", name, detail or "matched")
    elseif verdict == "skip" then
        record("SKIP", name, detail or "unavailable")
    elseif verdict == false then
        record("FAIL", name, detail or "check returned false")
    else
        record("FAIL", name, "unexpected result: " .. tostring(verdict))
    end
end

local function check_function(name, value)
    check(name, function()
        if type(value) ~= "function" then
            return false, "expected function, got " .. type(value)
        end
        return true, "function"
    end)
end

local function reset_results()
    status_by_name = {}
    for _, name in ipairs(api_names) do
        status_by_name[name] = {status = "SKIP", detail = "not tested"}
    end
    status_by_name["vesta.ui.consume"] = {status = "SKIP", detail = "not tested: Run again has not been pressed"}
    if off_test.result then
        status_by_name["vesta.events.off"] = off_test.result
    end
end

local function summary_counts()
    local counts = {PASS = 0, FAIL = 0, SKIP = 0}
    for _, name in ipairs(api_names) do
        local result = status_by_name[name]
        local status = result and result.status or "SKIP"
        if counts[status] == nil then
            status = "SKIP"
        end
        counts[status] = counts[status] + 1
    end
    return counts
end

local function refresh_summary()
    local counts = summary_counts()
    summary_text = string.format("PASS %d   FAIL %d   SKIP %d", counts.PASS, counts.FAIL, counts.SKIP)
    vesta.ui.text("run_summary", "Last run", summary_text)
    vesta.ui.text("events_summary", "Events", event_summary())
end

event_summary = function()
    local fired = {}
    local idle = {}
    for _, name in ipairs(event_names) do
        local count = event_counts[name] or 0
        if count > 0 then
            fired[#fired + 1] = name .. "(" .. tostring(count) .. ")"
        else
            idle[#idle + 1] = name
        end
    end
    return "fired: " .. (next(fired) and table.concat(fired, ", ") or "none") ..
        "; not fired: " .. (next(idle) and table.concat(idle, ", ") or "none")
end

local function run_api_checks()
    check("vesta.log", function()
        local ok, error_text = pcall(vesta.log, "[pasta_api_test] vesta.log check")
        if not ok then
            return false, "call raised: " .. tostring(error_text)
        end
        return true, "log call accepted"
    end)

    check("vesta.api.major", function()
        if type(vesta.api) ~= "table" then
            return false, "vesta.api is " .. type(vesta.api)
        end
        if vesta.api.major ~= 1 then
            return false, "expected 1, got " .. tostring(vesta.api.major)
        end
        return true, "value 1"
    end)

    check("vesta.api.minor", function()
        if type(vesta.api) ~= "table" then
            return false, "vesta.api is " .. type(vesta.api)
        end
        if vesta.api.minor ~= 0 then
            return false, "expected 0, got " .. tostring(vesta.api.minor)
        end
        return true, "value 0"
    end)

    check("vesta.api.script_dir", function()
        if type(vesta.api.script_dir) ~= "string" or #vesta.api.script_dir == 0 then
            return false, "expected non-empty string, got " .. short_value(vesta.api.script_dir)
        end
        local texture, error_text = vesta.draw.load_texture(PROBE_TEXTURE)
        if type(texture) ~= "string" or #texture == 0 then
            return "skip", "directory is non-empty but probe file was not found: " .. tostring(error_text)
        end
        return true, "probe file found under script_dir"
    end)

    check("vesta.api.data_dir", function()
        if type(vesta.api.data_dir) ~= "string" or #vesta.api.data_dir == 0 then
            return false, "expected non-empty string, got " .. short_value(vesta.api.data_dir)
        end
        local ok, accepted = pcall(vesta.storage.set, "_directory_probe", true)
        if not ok or accepted ~= true then
            return false, "storage probe could not write: " .. tostring(accepted)
        end
        local value = vesta.storage.get("_directory_probe")
        pcall(vesta.storage.remove, "_directory_probe")
        if value ~= true then
            return false, "storage probe read " .. short_value(value)
        end
        return true, "storage probe wrote and read under data_dir"
    end)
end

local function run_event_checks()
    check("vesta.events.on", function()
        local missing = {}
        local count = 0
        for _, name in ipairs(event_names) do
            local token = event_tokens[name]
            if not is_integer(token) or token <= 0 then
                missing[#missing + 1] = name
            else
                count = count + 1
            end
        end
        if next(missing) then
            return false, "not registered: " .. table.concat(missing, ", ")
        end
        return true, tostring(count) .. " event names returned integer tokens"
    end)

    if off_test.result then
        record(off_test.result.status, "vesta.events.off", off_test.result.detail)
    else
        record("SKIP", "vesta.events.off", "waiting for the tick sequence")
    end
end

local function run_storage_checks()
    check("vesta.storage.set", function()
        storage_value = {number = 12.5, text = "probe", flag = true, nested = {value = 7}}
        local ok, accepted = pcall(vesta.storage.set, STORAGE_KEY, storage_value)
        if not ok then
            return false, "call raised: " .. tostring(accepted)
        end
        if accepted ~= true then
            return false, "expected true, got " .. short_value(accepted)
        end
        return true, "returned true"
    end)

    check("vesta.storage.get", function()
        local value = vesta.storage.get(STORAGE_KEY)
        if type(value) ~= "table" then
            return false, "expected table, got " .. short_value(value)
        end
        if not deep_equal(value, storage_value) then
            return false, "roundtrip mismatch: " .. short_value(value)
        end
        local missing = vesta.storage.get(MISSING_STORAGE_KEY)
        if missing ~= nil then
            return false, "missing key returned " .. short_value(missing)
        end
        return true, "roundtrip matched and missing key returned nil"
    end)

    check("vesta.storage.remove", function()
        local ok, removed = pcall(vesta.storage.remove, STORAGE_KEY)
        if not ok then
            return false, "call raised: " .. tostring(removed)
        end
        if removed ~= true then
            return false, "expected true for an existing key, got " .. short_value(removed)
        end
        if vesta.storage.get(STORAGE_KEY) ~= nil then
            return false, "key still present after remove"
        end
        return true, "removed and missing key now nil"
    end)
end

local function validate_snapshot(value)
    if type(value) ~= "table" then
        return false, "expected table, got " .. type(value)
    end
    if not is_integer(value.sequence) or value.sequence < 0 then
        return false, "sequence: " .. short_value(value.sequence)
    end
    if not is_integer(value.timestamp_us) or value.timestamp_us <= 0 then
        return false, "timestamp_us: " .. short_value(value.timestamp_us)
    end
    if type(value.connected) ~= "boolean" then
        return false, "connected: " .. short_value(value.connected)
    end
    if type(value.menu_open) ~= "boolean" or type(value.input_ready) ~= "boolean" then
        return false, "menu_open/input_ready are not booleans"
    end
    if type(value.map) ~= "string" then
        return false, "map: " .. short_value(value.map)
    end
    if type(value.screen) ~= "table" or not is_integer(value.screen.width) or
        not is_integer(value.screen.height) or value.screen.width < 0 or value.screen.height < 0 then
        return false, "screen: " .. short_value(value.screen)
    end
    local camera = value.camera
    if type(camera) ~= "table" or not vec3_ok(camera.origin) or not vec3_ok(camera.angles) or
        not is_finite(camera.fov) then
        return false, "camera structure is invalid"
    end
    if type(camera.matrix) ~= "table" or #camera.matrix ~= 16 then
        return false, "camera.matrix length is not 16"
    end
    for index = 1, 16 do
        if not is_finite(camera.matrix[index]) then
            return false, "camera.matrix[" .. index .. "] is not finite"
        end
    end
    local player = value.local_player
    if type(player) ~= "table" or type(player.alive) ~= "boolean" or type(player.name) ~= "string" or
        not is_integer(player.team) or not is_integer(player.health) or not is_integer(player.tick_base) or
        not is_finite(player.game_time) or not is_finite(player.flash_alpha) or
        not is_integer(player.weapon_type) then
        return false, "local_player structure is invalid"
    end
    if type(value.players) ~= "table" or type(value.items) ~= "table" or
        type(value.projectiles) ~= "table" or type(value.spectators) ~= "table" then
        return false, "one of players/items/projectiles/spectators is not a table"
    end
    if value.players[1] ~= nil then
        local first = value.players[1]
        if type(first) ~= "table" or not is_integer(first.handle) or type(first.name) ~= "string" or
            not is_integer(first.health) or not is_integer(first.team) or not vec3_ok(first.origin) or
            not vec3_ok(first.eye_angles) or type(first.weapon) ~= "table" then
            return false, "players[1] structure is invalid"
        end
    end
    if value.items[1] ~= nil then
        local first = value.items[1]
        if type(first) ~= "table" or not is_integer(first.kind) or not vec3_ok(first.origin) then
            return false, "items[1] structure is invalid"
        end
    end
    if value.projectiles[1] ~= nil then
        local first = value.projectiles[1]
        if type(first) ~= "table" or not is_integer(first.id) or not is_integer(first.kind) or
            not vec3_ok(first.origin) or not vec3_ok(first.velocity) then
            return false, "projectiles[1] structure is invalid"
        end
    end
    local bomb = value.bomb
    if type(bomb) ~= "table" or type(bomb.active) ~= "boolean" or type(bomb.planted) ~= "boolean" or
        not vec3_ok(bomb.active_position) or not is_finite(bomb.time_remaining) or
        not is_finite(bomb.timer_length) or type(bomb.being_defused) ~= "boolean" or
        not is_finite(bomb.defuse_remaining) or not is_finite(bomb.defuse_length) or
        type(bomb.defuse_success) ~= "boolean" or not is_integer(bomb.site) or
        not is_integer(bomb.predicted_damage) or not vec3_ok(bomb.position) then
        return false, "bomb structure is invalid"
    end
    return true, "all checked fields have the expected types and ranges"
end

local function validate_radar_snapshot(value)
    if type(value) ~= "table" then
        return false, "expected table, got " .. type(value)
    end
    if not is_integer(value.sequence) or value.sequence < 0 then
        return false, "sequence: " .. short_value(value.sequence)
    end
    if not is_integer(value.timestamp_us) or value.timestamp_us <= 0 then
        return false, "timestamp_us: " .. short_value(value.timestamp_us)
    end
    if type(value.connected) ~= "boolean" or type(value.map) ~= "string" then
        return false, "connected/map types are invalid"
    end
    local camera = value.camera
    if type(camera) ~= "table" or not vec3_ok(camera.origin) or not vec3_ok(camera.angles) or
        not is_finite(camera.fov) then
        return false, "camera structure is invalid"
    end
    local player = value.local_player
    if type(player) ~= "table" or type(player.alive) ~= "boolean" or type(player.name) ~= "string" or
        not is_integer(player.team) or not is_integer(player.health) or not is_finite(player.game_time) then
        return false, "local_player structure is invalid"
    end
    if type(value.players) ~= "table" or type(value.items) ~= "table" or
        type(value.projectiles) ~= "table" or type(value.bomb) ~= "table" then
        return false, "one of players/items/projectiles/bomb is not a table"
    end
    local bomb = value.bomb
    if type(bomb.active) ~= "boolean" or not vec3_ok(bomb.active_position) or
        type(bomb.planted) ~= "boolean" or not is_finite(bomb.time_remaining) or
        type(bomb.being_defused) ~= "boolean" or not is_finite(bomb.defuse_remaining) or
        type(bomb.defuse_success) ~= "boolean" or not is_integer(bomb.site) or
        not is_integer(bomb.predicted_damage) or not vec3_ok(bomb.position) then
        return false, "bomb structure is invalid"
    end
    if value.players[1] ~= nil then
        local first = value.players[1]
        if type(first) ~= "table" or not is_integer(first.handle) or type(first.name) ~= "string" or
            not is_integer(first.health) or not vec3_ok(first.origin) or not vec3_ok(first.eye_angles) or
            type(first.weapon) ~= "table" or type(first.loadout) ~= "table" then
            return false, "players[1] structure is invalid"
        end
    end
    if value.items[1] ~= nil then
        local first = value.items[1]
        if type(first) ~= "table" or not is_integer(first.kind) or not vec3_ok(first.origin) then
            return false, "items[1] structure is invalid"
        end
    end
    if value.projectiles[1] ~= nil then
        local first = value.projectiles[1]
        if type(first) ~= "table" or not is_integer(first.id) or not vec3_ok(first.origin) or
            not vec3_ok(first.velocity) then
            return false, "projectiles[1] structure is invalid"
        end
    end
    return true, "all checked fields have the expected types and ranges"
end

local function camera_origin()
    local frame = game_snapshot_value
    if type(frame) == "table" and type(frame.camera) == "table" and vec3_ok(frame.camera.origin) then
        return frame.camera.origin
    end
    return nil
end

local function run_game_checks()
    game_snapshot_value = nil
    check("vesta.game.snapshot", function()
        local frame, error_text = vesta.game.snapshot()
        if frame == nil then
            return "skip", "no game snapshot is published (not connected)"
        end
        local ok, detail = validate_snapshot(frame)
        if not ok then
            return false, detail
        end
        game_snapshot_value = frame
        return true, detail .. "; connected=" .. tostring(frame.connected)
    end)

    check("vesta.game.radar_snapshot", function()
        local frame, error_text = vesta.game.radar_snapshot()
        if frame == nil then
            return "skip", "no radar snapshot is published (not connected)"
        end
        local ok, detail = validate_radar_snapshot(frame)
        if not ok then
            return false, detail
        end
        return true, detail .. "; connected=" .. tostring(frame.connected)
    end)

    check("vesta.game.world_to_screen", function()
        local origin = camera_origin()
        if origin == nil then
            return "skip", "no live camera origin"
        end
        local point = vesta.game.world_to_screen(origin)
        if point == nil then
            return "skip", "projection returned nil (off-screen or camera unavailable)"
        end
        if not vec2_ok(point) then
            return false, "expected {x,y} numbers, got " .. short_value(point)
        end
        return true, string.format("projected origin to %.1f, %.1f", point.x, point.y)
    end)

    check("vesta.game.trace_ray", function()
        local origin = camera_origin()
        if origin == nil then
            return "skip", "no live camera origin"
        end
        local finish = {x = origin.x, y = origin.y, z = origin.z + 64}
        local trace = vesta.game.trace_ray(origin, finish)
        if trace == nil then
            return "skip", "collision tracer returned nil (collision unavailable)"
        end
        if type(trace) ~= "table" or type(trace.hit) ~= "boolean" or not is_finite(trace.fraction) or
            trace.fraction < 0 or trace.fraction > 1 or not is_finite(trace.distance) or
            trace.distance < 0 or not vec3_ok(trace.position) or not vec3_ok(trace.normal) or
            not is_finite(trace.penetration) or not is_integer(trace.surface_type) then
            return false, "trace structure/range invalid: " .. short_value(trace)
        end
        return true, "hit=" .. tostring(trace.hit) .. ", fraction=" .. tostring(trace.fraction)
    end)

    check("vesta.game.bomb_damage", function()
        local damage = vesta.game.bomb_damage({x = 0, y = 0, z = 0}, 0, {x = 0, y = 0, z = 0}, false)
        if not is_integer(damage) then
            return false, "expected integer, got " .. short_value(damage)
        end
        if damage < -1 or damage > 1000 then
            return false, "damage outside -1..1000: " .. tostring(damage)
        end
        if damage < 0 then
            return "skip", "blast model has no damage for this position (returned -1)"
        end
        return true, "returned integer " .. tostring(damage)
    end)

    check("vesta.game.predict_grenade", function()
        local path, error_text = vesta.game.predict_grenade(
            {x = 0, y = 0, z = 0}, {x = 0, y = 0, z = 100}, "he", 2.0)
        if path == nil then
            return "skip", "grenade prediction unavailable: " .. tostring(error_text)
        end
        if type(path) ~= "table" or type(path.valid) ~= "boolean" or not is_finite(path.duration) or
            path.duration < 0 or not is_integer(path.end_tick) or path.end_tick < 0 or
            not vec3_ok(path.end_position) or type(path.points) ~= "table" or
            type(path.bounces) ~= "table" then
            return false, "trajectory structure/range invalid: " .. short_value(path)
        end
        if path.valid then
            for index, point in ipairs(path.points) do
                if not vec3_ok(point) then
                    return false, "points[" .. index .. "] is not a vec3"
                end
            end
            for index, point in ipairs(path.bounces) do
                if not vec3_ok(point) then
                    return false, "bounces[" .. index .. "] is not a vec3"
                end
            end
        end
        return true, "valid=" .. tostring(path.valid) .. ", points=" .. tostring(#path.points)
    end)

    check("vesta.game.radar_overview", function()
        local map_name = "de_dust2"
        if type(game_snapshot_value) == "table" and type(game_snapshot_value.map) == "string" and
            #game_snapshot_value.map > 0 then
            map_name = game_snapshot_value.map
        end
        local overview, error_text = vesta.game.radar_overview(map_name)
        if overview == nil then
            return "skip", "overview unavailable for " .. map_name .. ": " .. tostring(error_text)
        end
        if type(overview) ~= "table" or not is_finite(overview.pos_x) or not is_finite(overview.pos_y) or
            not is_finite(overview.scale) or overview.scale <= 0 or not is_integer(overview.width) or
            not is_integer(overview.height) or overview.width <= 0 or overview.height <= 0 or
            type(overview.has_lower) ~= "boolean" or not is_finite(overview.lower_altitude_max) or
            type(overview.primary_path) ~= "string" or #overview.primary_path == 0 then
            return false, "overview structure/range invalid: " .. short_value(overview)
        end
        if overview.has_lower and (type(overview.lower_path) ~= "string" or #overview.lower_path == 0) then
            return false, "has_lower is true but lower_path is missing"
        end
        return true, map_name .. " " .. tostring(overview.width) .. "x" .. tostring(overview.height)
    end)

    check("vesta.game.penetration_damage", function()
        local origin = camera_origin()
        if origin == nil then
            return "skip", "no live camera origin"
        end
        local result = vesta.game.penetration_damage(origin, {x = origin.x, y = origin.y, z = origin.z + 64})
        if result == nil then
            return "skip", "ballistics context unavailable"
        end
        if type(result) ~= "table" or not is_finite(result.damage) or result.damage < 0 or
            type(result.penetrated) ~= "boolean" then
            return false, "penetration structure/range invalid: " .. short_value(result)
        end
        return true, "damage=" .. tostring(result.damage) .. ", penetrated=" .. tostring(result.penetrated)
    end)
end

local function run_config_checks()
    check("vesta.config.get", function()
        local value = vesta.config.get("misc.m_bunny_hop.enabled")
        if type(value) ~= "boolean" then
            return false, "misc.m_bunny_hop.enabled is " .. short_value(value)
        end
        if vesta.config.get("pasta.api_test.missing") ~= nil then
            return false, "missing path did not return nil"
        end
        return true, "boolean path and missing path behaved correctly"
    end)

    check("vesta.config.snapshot", function()
        local snapshot = vesta.config.snapshot()
        if type(snapshot) ~= "table" then
            return false, "expected table, got " .. type(snapshot)
        end
        for _, section in ipairs({"combat_global", "combat_overrides", "esp", "misc"}) do
            if type(snapshot[section]) ~= "table" then
                return false, "section " .. section .. " is " .. type(snapshot[section])
            end
        end
        return true, "all four known sections are tables"
    end)

    check("vesta.config.schema", function()
        local schema = vesta.config.schema()
        if type(schema) ~= "table" or schema.type ~= "object" or type(schema.properties) ~= "table" then
            return false, "schema root is invalid"
        end
        local sections = 0
        for _, section in ipairs({"combat_global", "combat_overrides", "esp", "misc"}) do
            if schema.properties[section] == nil then
                return false, "schema missing section " .. section
            end
            sections = sections + 1
        end
        return true, tostring(sections) .. " schema sections present"
    end)

    start_config_write_checks()
end

local function run_ui_checks()
    check("vesta.ui.text", function()
        local value = vesta.ui.get("test_text")
        if type(value) ~= "string" or value ~= "hello" then
            return false, "expected string \"hello\", got " .. short_value(value)
        end
        return true, "read back \"hello\""
    end)

    check("vesta.ui.button", function()
        local value = vesta.ui.get("test_button")
        if type(value) ~= "boolean" or value ~= false then
            return false, "expected boolean false, got " .. short_value(value)
        end
        return true, "read back false before press"
    end)

    check("vesta.ui.toggle", function()
        local value = vesta.ui.get("test_toggle")
        if type(value) ~= "boolean" or value ~= true then
            return false, "expected boolean true, got " .. short_value(value)
        end
        return true, "read back true"
    end)

    check("vesta.ui.slider", function()
        local value = vesta.ui.get("test_slider")
        if type(value) ~= "number" or math.abs(value - 0.5) > 0.0001 then
            return false, "expected 0.5, got " .. short_value(value)
        end
        return true, "read back 0.5"
    end)

    check("vesta.ui.select", function()
        local value = vesta.ui.get("test_select")
        if not is_integer(value) or value ~= 1 then
            return false, "expected integer 1, got " .. short_value(value)
        end
        return true, "read back integer 1"
    end)

    check("vesta.ui.get", function()
        if vesta.ui.get("test_missing_control") ~= nil then
            return false, "missing id did not return nil"
        end
        if vesta.ui.get("test_text") ~= "hello" then
            return false, "known id did not return its text value"
        end
        return true, "known id and missing id behaved correctly"
    end)

    check("vesta.ui.input", function()
        local value = vesta.ui.get("test_input")
        if type(value) ~= "string" or value ~= "probe" then
            return false, "expected \"probe\", got " .. short_value(value)
        end
        return true, "read back \"probe\""
    end)

    check("vesta.ui.color", function()
        local value = vesta.ui.get("test_color")
        if type(value) ~= "table" or value.r ~= 12 or value.g ~= 34 or value.b ~= 56 or value.a ~= 78 then
            return false, "expected {12,34,56,78}, got " .. short_value(value)
        end
        return true, "read back all four channels"
    end)

    check("vesta.ui.keybind", function()
        local value = vesta.ui.get("test_keybind")
        if not is_integer(value) or value ~= 65 then
            return false, "expected integer 65, got " .. short_value(value)
        end
        return true, "read back integer 65"
    end)

    check("vesta.ui.separator", function()
        local value = vesta.ui.get("ui_separator")
        if type(value) ~= "string" then
            return false, "expected string, got " .. type(value)
        end
        return true, "read back " .. short_value(value)
    end)

    if consume_clicked then
        record("PASS", "vesta.ui.consume", "returned true after Run again press")
    else
        record("SKIP", "vesta.ui.consume", "not tested: Run again has not been pressed")
    end
end

local function run_draw_checks()
    probe_texture = nil
    check("vesta.draw.load_texture", function()
        local texture, error_text = vesta.draw.load_texture(PROBE_TEXTURE)
        if type(texture) ~= "string" or #texture == 0 then
            if tostring(error_text) == "texture file not found" then
                return "skip", "probe asset is not installed: " .. PROBE_TEXTURE
            end
            return false, "probe texture not loaded: " .. tostring(error_text)
        end
        probe_texture = texture
        return true, "returned texture key for " .. PROBE_TEXTURE
    end)

    check("vesta.draw.image", function()
        if type(vesta.draw.image) ~= "function" then
            return false, "function missing"
        end
        if type(probe_texture) ~= "string" then
            return "skip", "probe texture is not loaded"
        end
        return true, "image command uses the loaded probe key during frame draw"
    end)

    check_function("vesta.draw.line", vesta.draw and vesta.draw.line)
    check_function("vesta.draw.rect", vesta.draw and vesta.draw.rect)
    check_function("vesta.draw.filled_rect", vesta.draw and vesta.draw.filled_rect)
    check_function("vesta.draw.circle", vesta.draw and vesta.draw.circle)
    check_function("vesta.draw.filled_circle", vesta.draw and vesta.draw.filled_circle)
    check_function("vesta.draw.text", vesta.draw and vesta.draw.text)
    check_function("vesta.draw.world_line", vesta.draw and vesta.draw.world_line)
    check_function("vesta.draw.world_text", vesta.draw and vesta.draw.world_text)
    check_function("vesta.draw.panel", vesta.draw and vesta.draw.panel)
end

local function input_writes_enabled()
    local ok, value = pcall(vesta.ui.get, "allow_input_writes")
    return ok and value == true
end

local function run_input_checks()
    check("vesta.input.is_down", function()
        local value = vesta.input.is_down(32)
        if type(value) ~= "boolean" then
            return false, "expected boolean, got " .. type(value)
        end
        return true, "returned " .. tostring(value)
    end)

    check("vesta.input.binding", function()
        local value = vesta.input.binding("jump")
        if value == nil then
            return "skip", "jump binding is unavailable"
        end
        if type(value) ~= "table" or not is_integer(value.virtual_key) or
            value.virtual_key < 0 or value.virtual_key > 255 or not is_integer(value.device) or
            type(value.name) ~= "string" then
            return false, "binding structure/range invalid: " .. short_value(value)
        end
        return true, "jump binding returned key " .. tostring(value.virtual_key)
    end)

    local opt_in = input_writes_enabled()

    check("vesta.input.tap", function()
        if not opt_in then
            return "skip", "not tested: enable Allow input write tests"
        end
        local ok, accepted = pcall(vesta.input.tap, INPUT_KEY)
        if not ok then
            return false, "call raised: " .. tostring(accepted)
        end
        if accepted ~= true then
            return "skip", "input unavailable (menu, focus, Walk Bot, text entry, or no match)"
        end
        return true, "tap accepted"
    end)

    check("vesta.input.key", function()
        if not opt_in then
            return "skip", "not tested: enable Allow input write tests"
        end
        local ok, pressed = pcall(vesta.input.key, INPUT_KEY, true)
        if not ok then
            return false, "press raised: " .. tostring(pressed)
        end
        if pressed ~= true then
            pcall(vesta.input.key, INPUT_KEY, false)
            return "skip", "input unavailable (menu, focus, Walk Bot, text entry, or no match)"
        end
        local release_ok, released = pcall(vesta.input.key, INPUT_KEY, false)
        if not release_ok then
            return false, "release raised: " .. tostring(released)
        end
        if released ~= true then
            return false, "press was accepted but release returned " .. short_value(released)
        end
        return true, "press and release both accepted"
    end)

    check("vesta.input.mouse_move", function()
        if not opt_in then
            return "skip", "not tested: enable Allow input write tests"
        end
        local ok, accepted = pcall(vesta.input.mouse_move, 0, 0)
        if not ok then
            return false, "call raised: " .. tostring(accepted)
        end
        if accepted ~= true then
            return "skip", "input unavailable (menu, focus, Walk Bot, text entry, or no match)"
        end
        return true, "relative move (0,0) accepted"
    end)

    check("vesta.input.mouse_button", function()
        if not opt_in then
            return "skip", "not tested: enable Allow input write tests"
        end
        local ok, pressed = pcall(vesta.input.mouse_button, "secondary", true)
        if not ok then
            return false, "press raised: " .. tostring(pressed)
        end
        if pressed ~= true then
            pcall(vesta.input.mouse_button, "secondary", false)
            return "skip", "input unavailable (menu, focus, Walk Bot, text entry, or no match)"
        end
        local release_ok, released = pcall(vesta.input.mouse_button, "secondary", false)
        if not release_ok then
            return false, "release raised: " .. tostring(released)
        end
        if released ~= true then
            return false, "press was accepted but release returned " .. short_value(released)
        end
        return true, "secondary press and release both accepted"
    end)
end

local function run_asset_checks()
    check("vesta.assets.weapon_font", function()
        local font = vesta.assets.weapon_font()
        if type(font) ~= "string" then
            return false, "expected string, got " .. type(font)
        end
        if #font < 1024 then
            return false, "font is only " .. tostring(#font) .. " bytes"
        end
        local b1, b2, b3, b4 = font:byte(1, 4)
        if b1 ~= 0 or b2 ~= 1 or b3 ~= 0 or b4 ~= 0 then
            return false, string.format("unexpected header %d %d %d %d", b1, b2, b3, b4)
        end
        return true, tostring(#font) .. " bytes with TrueType header"
    end)

    record("SKIP", "vesta.helpers.start", "not exposed: helpers are available to trusted scripts only")
    record("SKIP", "vesta.helpers.copy_text", "not exposed: helpers are available to trusted scripts only")
end

local function finalize_missing()
    for _, name in ipairs(api_names) do
        if status_by_name[name] == nil then
            status_by_name[name] = {status = "SKIP", detail = "not tested: check did not run"}
        end
    end
end

local function log_summary()
    local counts = summary_counts()
    local line = string.format("SUMMARY passed=%d failed=%d skipped=%d", counts.PASS, counts.FAIL, counts.SKIP)
    pcall(vesta.log, line)
    pcall(vesta.log, "EVENTS " .. event_summary())
    if log_error then
        pcall(vesta.log, "LOG_ERROR " .. log_error)
    end
end

start_config_write_checks = function()
    local opt_in = false
    local ok, value = pcall(vesta.ui.get, "allow_config_writes")
    if ok then
        opt_in = value == true
    end
    if not opt_in then
        record("SKIP", "vesta.config.set", "not tested: enable Allow config write tests")
        record("SKIP", "vesta.config.patch", "not tested: enable Allow config write tests")
        return
    end
    local get_ok, original = pcall(vesta.config.get, CONFIG_PATH)
    if not get_ok then
        record("FAIL", "vesta.config.set", "read raised: " .. tostring(original))
        record("SKIP", "vesta.config.patch", "not tested: set could not start")
        return
    end
    if type(original) ~= "boolean" then
        record("FAIL", "vesta.config.set", "original value is " .. short_value(original))
        record("SKIP", "vesta.config.patch", "not tested: set could not start")
        return
    end
    local expected = not original
    local set_ok, accepted = pcall(vesta.config.set, CONFIG_PATH, expected)
    if not set_ok then
        record("FAIL", "vesta.config.set", "call raised: " .. tostring(accepted))
        record("SKIP", "vesta.config.patch", "not tested: set could not start")
        return
    end
    if accepted ~= true then
        record("FAIL", "vesta.config.set", "queue returned " .. short_value(accepted))
        record("SKIP", "vesta.config.patch", "not tested: set could not start")
        return
    end
    pending_config = {
        stage = "set",
        original = original,
        expected = expected,
        ticks = 0,
    }
    status_by_name["vesta.config.set"] = {status = "SKIP", detail = "pending: queued " .. tostring(expected)}
    status_by_name["vesta.config.patch"] = {status = "SKIP", detail = "pending: waiting for set result"}
end

local function process_pending_config()
    if pending_config == nil then
        return
    end
    pending_config.ticks = pending_config.ticks + 1
    if pending_config.ticks < 20 then
        return
    end

    if pending_config.stage == "set" then
        local read_ok, current = pcall(vesta.config.get, CONFIG_PATH)
        if not read_ok then
            record("FAIL", "vesta.config.set", "read raised: " .. tostring(current))
        elseif current ~= pending_config.expected then
            record("FAIL", "vesta.config.set", "expected " ..
                tostring(pending_config.expected) .. ", got " .. short_value(current))
        else
            record("PASS", "vesta.config.set", "value " ..
                tostring(pending_config.expected) .. " was applied")
        end
        local patch_ok, accepted = pcall(vesta.config.patch, {[CONFIG_PATH] = pending_config.original})
        if not patch_ok or accepted ~= true then
            record("FAIL", "vesta.config.patch", "patch queue returned " ..
                short_value(accepted))
            pcall(vesta.config.set, CONFIG_PATH, pending_config.original)
            pending_config = nil
            log_summary()
            return
        end
        pending_config.stage = "patch"
        pending_config.ticks = 0
        status_by_name["vesta.config.patch"] = {status = "SKIP", detail = "pending: waiting for restore"}
        return
    end

    local read_ok, current = pcall(vesta.config.get, CONFIG_PATH)
    if not read_ok then
        record("FAIL", "vesta.config.patch", "read raised: " .. tostring(current))
    elseif current ~= pending_config.original then
        record("FAIL", "vesta.config.patch", "expected restore " ..
            tostring(pending_config.original) .. ", got " .. short_value(current))
    else
        record("PASS", "vesta.config.patch", "patch restored " ..
            tostring(pending_config.original))
    end
    pending_config = nil
    log_summary()
end

local function run_tests()
    if pending_config ~= nil then
        pcall(vesta.config.patch, {[CONFIG_PATH] = pending_config.original})
        pending_config = nil
    end
    reset_results()
    run_api_checks()
    run_event_checks()
    run_storage_checks()
    run_game_checks()
    run_config_checks()
    run_ui_checks()
    run_draw_checks()
    run_input_checks()
    run_asset_checks()
    finalize_missing()
    log_summary()
end

local function process_off_test()
    if off_test.phase == 1 then
        off_test.seen = off_test.seen + 1
        if off_test.seen >= 3 then
            local ok, removed = pcall(vesta.events.off, "tick", off_test.token)
            if not ok then
                off_test.result = {status = "FAIL", detail = "off raised: " .. tostring(removed)}
                off_test.phase = 3
            elseif removed ~= true then
                off_test.result = {status = "FAIL", detail = "off returned " .. short_value(removed)}
                off_test.phase = 3
            else
                off_test.baseline = nil
                off_test.phase = 2
                off_test.seen = 0
            end
            if off_test.result then
                status_by_name["vesta.events.off"] = off_test.result
                pcall(vesta.log, "FAIL vesta.events.off - " .. off_test.result.detail)
                log_summary()
            end
        end
    elseif off_test.phase == 2 then
        off_test.seen = off_test.seen + 1
        if off_test.seen == 2 then
            off_test.baseline = off_test.count
        elseif off_test.seen >= 5 then
            if off_test.count == off_test.baseline then
                off_test.result = {status = "PASS", detail = "handler stopped after off; count stayed " ..
                    tostring(off_test.baseline)}
            else
                off_test.result = {status = "FAIL", detail = "handler fired after off: " ..
                    tostring(off_test.baseline) .. " -> " .. tostring(off_test.count)}
            end
            off_test.phase = 3
            status_by_name["vesta.events.off"] = off_test.result
            pcall(vesta.log, off_test.result.status .. " vesta.events.off - " .. off_test.result.detail)
            log_summary()
        end
    end
end

local function draw_call(name, fn)
    local ok, error_text = pcall(fn)
    if not ok and not draw_failures[name] then
        draw_failures[name] = true
        record("FAIL", name, "draw raised: " .. tostring(error_text))
    end
end

local function frame_screen(frame)
    local width = 1920
    local height = 1080
    if type(frame) == "table" and type(frame.screen) == "table" then
        width = frame.screen.width or width
        height = frame.screen.height or height
    end
    return width, height
end

local function draw_visual(frame)
    local x, y, width, height = 24, 24, 380, 224
    draw_call("vesta.draw.panel", function()
        vesta.draw.panel(x, y, width, height, "DRAW API TEST", 10, 12, 18, 230)
    end)
    draw_call("vesta.draw.line", function()
        vesta.draw.line(x + 16, y + 52, x + width - 16, y + 52, 255, 80, 80, 255, 2)
    end)
    draw_call("vesta.draw.rect", function()
        vesta.draw.rect(x + 16, y + 66, width - 32, 32, 80, 200, 255, 255, 2)
    end)
    draw_call("vesta.draw.filled_rect", function()
        vesta.draw.filled_rect(x + 22, y + 72, width - 44, 20, 30, 50, 90, 180)
    end)
    draw_call("vesta.draw.circle", function()
        vesta.draw.circle(x + 52, y + 144, 18, 255, 210, 80, 255, 2)
    end)
    draw_call("vesta.draw.filled_circle", function()
        vesta.draw.filled_circle(x + 100, y + 144, 14, 255, 120, 40, 180)
    end)
    draw_call("vesta.draw.text", function()
        vesta.draw.text(x + 16, y + 176, "line rect circle text image", 240, 240, 240, 255)
    end)
    if type(probe_texture) == "string" then
        draw_call("vesta.draw.image", function()
            vesta.draw.image(probe_texture, x + width - 52, y + 124, 16, 16, 255, 255, 255, 255)
        end)
    end
    local origin = {x = 0, y = 0, z = 0}
    if type(frame) == "table" and type(frame.camera) == "table" and vec3_ok(frame.camera.origin) then
        origin = frame.camera.origin
    end
    draw_call("vesta.draw.world_line", function()
        vesta.draw.world_line(origin, {x = origin.x, y = origin.y, z = origin.z + 64},
            80, 255, 120, 255, 1)
    end)
    draw_call("vesta.draw.world_text", function()
        vesta.draw.world_text({x = origin.x, y = origin.y, z = origin.z + 64},
            "world", 120, 255, 180, 255)
    end)
end

local function draw_results(frame)
    local screen_width, screen_height = frame_screen(frame)
    local x = 24
    local y = 270
    local width = math.min(900, math.max(360, screen_width - 48))
    local height = math.min(760, math.max(200, screen_height - y - 24))
    vesta.draw.panel(x, y, width, height, "PASTA LUA API TEST", 10, 12, 18, 230)
    vesta.draw.text(x + 14, y + 28, summary_text, 240, 240, 240, 255)
    vesta.draw.text(x + 14, y + 44, "v" .. API_VERSION .. "  " ..
        (pending_config and "config write test pending" or "look at the log for full details"),
        170, 180, 200, 255)

    local columns = 1
    if width >= 720 and #api_names > 28 then
        columns = 2
    end
    local per_column = math.ceil(#api_names / columns)
    local column_width = math.floor(width / columns)
    local line_y = y + 64
    for index, name in ipairs(api_names) do
        local column = math.floor((index - 1) / per_column)
        local row = (index - 1) % per_column
        local text_x = x + 14 + column * column_width
        local text_y = line_y + row * 13
        if text_y < y + height - 34 then
            local result = status_by_name[name] or {status = "SKIP", detail = "not tested"}
            local red, green, blue = 220, 220, 220
            if result.status == "PASS" then
                red, green, blue = 130, 230, 150
            elseif result.status == "FAIL" then
                red, green, blue = 255, 110, 110
            elseif result.status == "SKIP" then
                red, green, blue = 255, 205, 110
            end
            local line = name .. "  " .. result.status
            if result.status == "FAIL" then
                line = line .. ": " .. result.detail
            end
            vesta.draw.text(text_x, text_y, line, red, green, blue, 255)
        end
    end

    local events = event_summary()
    if #events > 140 then
        events = events:sub(1, 137) .. "..."
    end
    vesta.draw.text(x + 14, y + height - 28, events, 170, 180, 200, 255)
end

local function draw_frame(frame)
    local show_ok, show = pcall(vesta.ui.get, "show_panel")
    if show_ok and show == false then
        return
    end
    local ok, error_text = pcall(draw_visual, frame)
    if not ok then
        record("FAIL", "vesta.draw.panel", "visual draw error: " .. tostring(error_text))
    end
    if not results_draw_failed then
        local results_ok, results_error = pcall(draw_results, frame)
        if not results_ok then
            results_draw_failed = true
            record("FAIL", "vesta.draw.text", "results panel error: " .. tostring(results_error))
        end
    end
end

local function tick_update()
    local clicked = vesta.ui.consume("run")
    if clicked then
        consume_clicked = true
        local ok, error_text = pcall(run_tests)
        consume_clicked = false
        if not ok then
            record("FAIL", "vesta.log", "run_tests raised: " .. tostring(error_text))
        end
        local consume_result = status_by_name["vesta.ui.consume"]
        if consume_result == nil or consume_result.status ~= "PASS" then
            record(ok and "PASS" or "FAIL", "vesta.ui.consume",
                ok and "returned true after Run again press" or "run_tests raised after press")
        end
        if not ok then
            log_summary()
        end
    end
    local off_ok, off_error = pcall(process_off_test)
    if not off_ok then
        status_by_name["vesta.events.off"] = {status = "FAIL", detail = "off sequence raised: " ..
            tostring(off_error)}
    end
    local config_ok, config_error = pcall(process_pending_config)
    if not config_ok then
        record("FAIL", "vesta.config.patch", "config sequence raised: " .. tostring(config_error))
        pending_config = nil
        log_summary()
    end
    local summary_ok, summary_error = pcall(refresh_summary)
    if not summary_ok then
        record("FAIL", "vesta.ui.text", "summary update raised: " .. tostring(summary_error))
    end
end

local function setup_controls()
    vesta.ui.button("run", "Run again", "Run")
    vesta.ui.toggle("allow_config_writes", "Allow config write tests", false)
    vesta.ui.toggle("allow_input_writes", "Allow input write tests", false)
    vesta.ui.toggle("show_panel", "Show test results", true)
    vesta.ui.separator("ui_separator", "UI API checks")
    vesta.ui.toggle("test_toggle", "Toggle value", true)
    vesta.ui.slider("test_slider", "Slider value", 0.5, 0.0, 1.0, 0.05)
    vesta.ui.select("test_select", "Select value", {"zero", "one", "two"}, 1)
    vesta.ui.input("test_input", "Input value", "probe")
    vesta.ui.color("test_color", "Color value", {r = 12, g = 34, b = 56, a = 78})
    vesta.ui.keybind("test_keybind", "Keybind value", 65)
    vesta.ui.text("test_text", "Text value", "hello")
    vesta.ui.button("test_button", "Button value", "Press")
    vesta.ui.text("run_summary", "Last run", "Not run yet")
    vesta.ui.text("events_summary", "Events", "Waiting for events")
end

local function setup_events()
    for _, name in ipairs(event_names) do
        event_counts[name] = 0
        local event_name = name
        local ok, token = pcall(vesta.events.on, event_name, function(...)
            event_counts[event_name] = (event_counts[event_name] or 0) + 1
            if event_name == "tick" then
                local tick_ok, tick_error = pcall(tick_update)
                if not tick_ok then
                    record("FAIL", "vesta.events.on", "tick handler raised: " .. tostring(tick_error))
                end
            elseif event_name == "frame" then
                local frame_ok, frame_error = pcall(draw_frame, ...)
                if not frame_ok then
                    record("FAIL", "vesta.draw.panel", "frame handler raised: " .. tostring(frame_error))
                end
            elseif event_name == "load" then
                local load_ok, load_error = pcall(run_tests)
                if not load_ok then
                    record("FAIL", "vesta.log", "load test raised: " .. tostring(load_error))
                end
            end
        end)
        if ok then
            event_tokens[event_name] = token
        else
            event_errors[event_name] = tostring(token)
        end
    end

    local ok, token = pcall(vesta.events.on, "tick", function()
        off_test.count = off_test.count + 1
    end)
    if not ok or not is_integer(token) then
        off_test.phase = 3
        off_test.result = {status = "FAIL", detail = "temporary tick handler registration failed: " ..
            tostring(token)}
        status_by_name["vesta.events.off"] = off_test.result
    else
        off_test.token = token
        off_test.phase = 1
    end
end

setup_controls()
setup_events()
