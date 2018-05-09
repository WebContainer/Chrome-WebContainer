enum SystemCallName {
    Open,
    Close,
    Socket,
    Fork,
    Exec
};

struct SystemCall {
    SystemCallName name;
    char message[10];
};

struct SystemCallReturn {
    int code;
};
