"""Share read-only return hooks when banked calls use the same trampoline."""
def watch_return(g,key,callback):
    if not hasattr(g,'_return_observers'):g._return_observers={}
    if key not in g._return_observers:
        g._return_observers[key]=[]
        def dispatch(at):
            for fn in g._return_observers[at]:fn(at)
        g.pb.hook_register(*key,dispatch,key)
    g._return_observers[key].append(callback)
