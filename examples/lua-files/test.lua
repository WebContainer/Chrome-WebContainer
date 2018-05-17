print("hello Cheng and Matt, from lua")
repeat -- REPL
    io.write'> '
    io.stdout:flush()
    local s = io.read()
    if s == 'exit' then break end
    print(s)
until false
