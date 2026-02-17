#!/usr/bin/env lua

local output_path = arg[1] or "builtins.json"

-- ==============================================================================
-- Minimal JSON encoder (no external dependency)
-- ==============================================================================

local function json_str(s)
    return '"' .. s:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n') .. '"'
end

local json_encode  -- forward

local function json_array(t)
    local parts = {}
    for _, v in ipairs(t) do parts[#parts+1] = json_encode(v) end
    return "[" .. table.concat(parts, ",") .. "]"
end

local function json_object(t)
    local parts = {}
    local keys = {}
    for k in pairs(t) do keys[#keys+1] = k end
    table.sort(keys, function(a,b) return tostring(a) < tostring(b) end)
    for _, k in ipairs(keys) do
        parts[#parts+1] = json_str(tostring(k)) .. ":" .. json_encode(t[k])
    end
    return "{" .. table.concat(parts, ",") .. "}"
end

json_encode = function(val)
    if val == nil  then return "null" end
    local ty = type(val)
    if ty == "boolean" then return val and "true" or "false" end
    if ty == "number"  then
        if val ~= val then return '"NaN"' end
        if val == math.huge then return '1e999' end
        if val == -math.huge then return '-1e999' end
        if val == math.floor(val) then return string.format("%d", val) end
        return string.format("%.17g", val)
    end
    if ty == "string"  then return json_str(val) end
    if ty == "table"   then
        if val[1] ~= nil or next(val) == nil then
            local n = #val
            local all_int = true
            for k in pairs(val) do
                if type(k) ~= "number" or k < 1 or k > n or k ~= math.floor(k) then
                    all_int = false; break
                end
            end
            if all_int then return json_array(val) end
        end
        return json_object(val)
    end
    return '"<' .. ty .. '>"'
end

-- ==============================================================================
-- Type IDs  (must match the "types" array emitted)
-- ==============================================================================
local T = {
    object   = 0,
    ["nil"]  = 1,
    boolean  = 2,
    number   = 3,
    string   = 4,
    table    = 5,
}
local type_list = {"object", "nil", "boolean", "number", "string", "table"}

-- ==============================================================================
-- Helpers
-- ==============================================================================
local function sig(params, ret, selfType)
    return {
        paramTypes = params or {},
        returnType = ret  or -1,
        selfType   = selfType or -1,
    }
end

local function func(name, params, ret, selfType, isConst)
    return { name = name, isCallable = true, isConst = isConst == nil and true or isConst,
             scope = 0, type = ret or 0,
             funcSig = sig(params, ret, selfType) }
end

local function const_prop(name, tid)
    return { name = name, isCallable = false, isConst = true, scope = 0,
             type = tid,
             funcSig = sig({}, -1, -1) }
end

-- ==============================================================================
-- Known type signatures for standard library functions.
--
-- Lua is dynamically typed so we can't introspect parameter types. Instead we
-- maintain a table of  {paramTypes, returnType} keyed by function name.
-- Discovery of *which* functions exist is done dynamically (see below);
-- this table only provides type annotations for the ones we know about.
-- Functions not listed here get a fallback signature of ({object}, object).
-- ==============================================================================
local SIGS = {}

-- Global functions
SIGS["type"]          = { {T.object},                       T.string  }
SIGS["tostring"]      = { {T.object},                       T.string  }
SIGS["tonumber"]      = { {T.object},                       T.number  }
SIGS["assert"]        = { {T.object},                       T.object  }
SIGS["pcall"]         = { {T.object},                       T.boolean }
SIGS["xpcall"]        = { {T.object, T.object},             T.boolean }
SIGS["rawget"]        = { {T.table, T.object},              T.object  }
SIGS["rawset"]        = { {T.table, T.object, T.object},    T.table   }
SIGS["rawlen"]        = { {T.table},                        T.number  }
SIGS["rawequal"]      = { {T.object, T.object},             T.boolean }
SIGS["getmetatable"]  = { {T.object},                       T.table   }
SIGS["setmetatable"]  = { {T.table, T.table},               T.table   }
SIGS["select"]        = { {T.number, T.object},             T.object  }
SIGS["next"]          = { {T.table},                        T.object  }
SIGS["pairs"]         = { {T.table},                        T.object  }
SIGS["ipairs"]        = { {T.table},                        T.object  }
SIGS["error"]         = { {T.object},                       -1        }
SIGS["warn"]          = { {T.string},                       -1        }
SIGS["collectgarbage"]= { {},                               T.number  }
SIGS["require"]       = { {T.string},                       T.object  }
SIGS["load"]          = { {T.string},                       T.object  }
SIGS["unpack"]        = { {T.table},                        T.object  }

-- string library
SIGS["string.byte"]    = { {T.string},                       T.number  }
SIGS["string.char"]    = { {T.number},                       T.string  }
SIGS["string.find"]    = { {T.string, T.string},             T.number  }
SIGS["string.format"]  = { {T.string, T.object},             T.string  }
SIGS["string.gmatch"]  = { {T.string, T.string},             T.object  }
SIGS["string.gsub"]    = { {T.string, T.string, T.string},   T.string  }
SIGS["string.len"]     = { {T.string},                       T.number  }
SIGS["string.lower"]   = { {T.string},                       T.string  }
SIGS["string.match"]   = { {T.string, T.string},             T.string  }
SIGS["string.rep"]     = { {T.string, T.number},             T.string  }
SIGS["string.reverse"] = { {T.string},                       T.string  }
SIGS["string.sub"]     = { {T.string, T.number},             T.string  }
SIGS["string.upper"]   = { {T.string},                       T.string  }
SIGS["string.dump"]    = { {T.object},                       T.string  }
SIGS["string.pack"]    = { {T.string, T.object},             T.string  }
SIGS["string.packsize"]= { {T.string},                       T.number  }
SIGS["string.unpack"]  = { {T.string, T.string},             T.object  }

-- table library
SIGS["table.concat"]  = { {T.table},                        T.string  }
SIGS["table.insert"]  = { {T.table, T.object},              -1        }
SIGS["table.move"]    = { {T.table, T.number, T.number, T.number}, T.table }
SIGS["table.remove"]  = { {T.table},                        T.object  }
SIGS["table.sort"]    = { {T.table},                        -1        }
SIGS["table.unpack"]  = { {T.table},                        T.object  }
SIGS["table.pack"]    = { {T.object},                       T.table   }

-- math library
SIGS["math.abs"]       = { {T.number},            T.number }
SIGS["math.acos"]      = { {T.number},            T.number }
SIGS["math.asin"]      = { {T.number},            T.number }
SIGS["math.atan"]      = { {T.number},            T.number }
SIGS["math.ceil"]      = { {T.number},            T.number }
SIGS["math.cos"]       = { {T.number},            T.number }
SIGS["math.deg"]       = { {T.number},            T.number }
SIGS["math.exp"]       = { {T.number},            T.number }
SIGS["math.floor"]     = { {T.number},            T.number }
SIGS["math.fmod"]      = { {T.number, T.number},  T.number }
SIGS["math.log"]       = { {T.number},            T.number }
SIGS["math.max"]       = { {T.number, T.number},  T.number }
SIGS["math.min"]       = { {T.number, T.number},  T.number }
SIGS["math.rad"]       = { {T.number},            T.number }
SIGS["math.random"]    = { {},                     T.number }
SIGS["math.randomseed"]= { {T.number},            -1       }
SIGS["math.sin"]       = { {T.number},            T.number }
SIGS["math.sqrt"]      = { {T.number},            T.number }
SIGS["math.tan"]       = { {T.number},            T.number }
SIGS["math.tointeger"] = { {T.number},            T.number }
SIGS["math.ult"]       = { {T.number, T.number},  T.boolean }
SIGS["math.type"]      = { {T.object},            T.string }
SIGS["math.modf"]      = { {T.number},            T.number }

-- coroutine library
SIGS["coroutine.create"]      = { {T.object},   T.object  }
SIGS["coroutine.resume"]      = { {T.object},   T.boolean }
SIGS["coroutine.yield"]       = { {},            T.object  }
SIGS["coroutine.wrap"]        = { {T.object},   T.object  }
SIGS["coroutine.status"]      = { {T.object},   T.string  }
SIGS["coroutine.isyieldable"] = { {},            T.boolean }
SIGS["coroutine.running"]     = { {},            T.object  }
SIGS["coroutine.close"]       = { {T.object},   T.boolean }

-- ==============================================================================
-- Functions/globals to skip (unsafe for fuzzing or not useful)
-- ==============================================================================
local BLACKLIST = {
    print    = true,  -- I/O
    dofile   = true,  -- filesystem
    loadfile = true,  -- filesystem
    input    = true,  -- I/O
    output   = true,  -- I/O
    read     = true,  -- I/O
    write    = true,  -- I/O
    close    = true,  -- I/O
    -- skip non-function globals
    _VERSION = true,
    _G       = true,
    arg      = true,
}

-- Libraries to skip entirely
local LIB_BLACKLIST = {
    io     = true,  -- file I/O — dangerous
    os     = true,  -- OS calls — dangerous
    debug  = true,  -- debug library — can break internals
    package = true, -- package loading — not useful
}

-- ==============================================================================
-- Dynamic discovery: iterate a table, match against SIGS, emit entries
-- ==============================================================================
local function discover_functions(lib, prefix)
    local result = {}
    local keys = {}
    for k in pairs(lib) do
        if type(k) == "string" then keys[#keys+1] = k end
    end
    table.sort(keys)

    for _, name in ipairs(keys) do
        local val = lib[name]
        local full = prefix and (prefix .. "." .. name) or name
        if BLACKLIST[name] or BLACKLIST[full] then
            goto continue
        end

        if type(val) == "function" then
            local s = SIGS[full]
            if s then
                result[#result+1] = func(name, s[1], s[2], -1, true)
            else
                -- Unknown function: probe how many args it takes
                -- by calling with 0..4 args and seeing what doesn't error
                -- with "bad argument". Fallback: single object param.
                local best_params = {T.object}
                local best_ret = T.object
                for n = 0, 4 do
                    local args = {}
                    for i = 1, n do args[i] = 0 end  -- dummy number args
                    local ok = pcall(val, table.unpack(args))
                    if ok then
                        best_params = {}
                        for i = 1, n do best_params[i] = T.object end
                        break
                    end
                end
                result[#result+1] = func(name, best_params, best_ret, -1, true)
                io.stderr:write(string.format(
                    "[warn] no signature for %s, using fallback (%d params)\n",
                    full, #best_params))
            end
        elseif type(val) == "number" then
            result[#result+1] = const_prop(name, T.number)
        elseif type(val) == "string" then
            result[#result+1] = const_prop(name, T.string)
        elseif type(val) == "boolean" then
            result[#result+1] = const_prop(name, T.boolean)
        end

        ::continue::
    end
    return result
end

-- ==============================================================================
-- Discover globals
-- ==============================================================================
local globals = {}
do
    local skip_tables = {
        string = true, table = true, math = true, coroutine = true,
        io = true, os = true, debug = true, package = true, utf8 = true,
    }
    local keys = {}
    for k in pairs(_G) do
        if type(k) == "string" then keys[#keys+1] = k end
    end
    table.sort(keys)

    for _, name in ipairs(keys) do
        local val = _G[name]
        if BLACKLIST[name] then goto continue end
        if type(val) == "table" and skip_tables[name] then goto continue end

        if type(val) == "function" then
            local s = SIGS[name]
            if s then
                globals[#globals+1] = func(name, s[1], s[2], -1, true)
            else
                globals[#globals+1] = func(name, {T.object}, T.object, -1, true)
                io.stderr:write(string.format(
                    "[warn] no signature for global '%s', using fallback\n", name))
            end
        elseif type(val) == "number" then
            globals[#globals+1] = const_prop(name, T.number)
        elseif type(val) == "string" then
            globals[#globals+1] = const_prop(name, T.string)
        elseif type(val) == "boolean" then
            globals[#globals+1] = const_prop(name, T.boolean)
        end
        ::continue::
    end
end

-- ==============================================================================
-- Discover library tables: string, table, math, coroutine
-- ==============================================================================
local LIBRARIES = {
    { lib = string,    name = "string",    tid = T.string },
    { lib = table,     name = "table",     tid = T.table  },
}

-- Libraries that need their own type entry (math, coroutine)
local EXTRA_LIBS = {}
for _, info in ipairs({
    { lib = math,      name = "math"      },
    { lib = coroutine, name = "coroutine"  },
}) do
    if info.lib and not LIB_BLACKLIST[info.name] then
        type_list[#type_list+1] = info.name
        local tid = #type_list - 1  -- 0-based
        EXTRA_LIBS[#EXTRA_LIBS+1] = { lib = info.lib, name = info.name, tid = tid }
    end
end

local ALL_LIBS = {}
for _, v in ipairs(LIBRARIES) do ALL_LIBS[#ALL_LIBS+1] = v end
for _, v in ipairs(EXTRA_LIBS) do ALL_LIBS[#ALL_LIBS+1] = v end

local lib_funcs = {}  -- tid → list of func entries
for _, info in ipairs(ALL_LIBS) do
    if info.lib and not LIB_BLACKLIST[info.name] then
        lib_funcs[info.tid] = discover_functions(info.lib, info.name)
    end
end

-- ==============================================================================
-- Operator compatibility tables  (probed dynamically)
-- ==============================================================================

local lua_bin_ops = {
    function(a,b) return a+b  end,
    function(a,b) return a-b  end,
    function(a,b) return a*b  end,
    function(a,b) return a/b  end,
    function(a,b) return a%b  end,
    function(a,b) return a^b  end,   -- power   (maps to **)
    function(a,b) return a//b end,   -- floor   (maps to //)
    function(a,b) return a==b end,
    function(a,b) return a~=b end,
    function(a,b) return a<b  end,
    function(a,b) return a>b  end,
    function(a,b) return a<=b end,
    function(a,b) return a>=b end,
    function(a,b) return a&b  end,   -- bitwise and
    function(a,b) return a|b  end,   -- bitwise or
    function(a,b) return a~b  end,   -- bitwise xor  (maps to Python ^)
    function(a,b) return a<<b end,
    function(a,b) return a>>b end,
}

local reps = {
    [T.boolean] = true,
    [T.number]  = 42,
    [T.string]  = "hello",
    [T.table]   = {},
}

local function safe(f, ...)
    local ok = pcall(f, ...)
    return ok
end

local base_type_count = 6  -- object, nil, boolean, number, string, table

local ops_table = {}
for _, opfn in ipairs(lua_bin_ops) do
    local row = {{}}
    for i = 1, base_type_count - 1 do
        local success_list = {}
        local a = reps[i]
        if a ~= nil then
            for j = 1, base_type_count - 1 do
                local b = reps[j]
                if b ~= nil and safe(opfn, a, b) then
                    success_list[#success_list+1] = j
                end
            end
        end
        row[#row+1] = success_list
    end
    ops_table[#ops_table+1] = row
end

local lua_unary_ops = {
    function(a) return -a    end,
    function(a) return not a end,
    function(a) return ~a    end,
}

local uops_table = {{}}
for _, opfn in ipairs(lua_unary_ops) do
    local success_list = {}
    for i = 1, base_type_count - 1 do
        local a = reps[i]
        if a ~= nil and safe(opfn, a) then
            success_list[#success_list+1] = i
        end
    end
    uops_table[#uops_table+1] = success_list
end

-- ==============================================================================
-- Assemble & write
-- ==============================================================================
local funcs = { ["-1"] = globals }
for tid, entries in pairs(lib_funcs) do
    funcs[tostring(tid)] = entries
end

local result = {
    types   = type_list,
    funcs   = funcs,
    modules = {},
    ops     = ops_table,
    uops    = uops_table,
}

local out = io.open(output_path, "w")
out:write(json_encode(result))
out:write("\n")
out:close()

local total = #globals
for _, entries in pairs(lib_funcs) do total = total + #entries end
print(string.format("[+] builtins.json written to %s  (%d entries, %d types)",
      output_path, total, #type_list))
