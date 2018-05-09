enum SystemCallName {
    Open,
    Close,
    Socket,
    Fork,
    Exec
};

struct SystemCall {
    SystemCallName name;
};

struct SystemCallReturn {
    int code;
};

#ifdef DEBUG
#else
#endif
