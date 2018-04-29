
import subprocess, os
import time
import shlex
import signal
from pyreuse.helpers import *
from pyreuse.sysutils.cgroup import *
from pyreuse.macros import *
from pyreuse.sysutils.iostat_parser import parse_iostat
from pyreuse.general.expbase import *
from pyreuse.sysutils.blocktrace import BlockTraceManager
import glob


######################
# System wide
######################
server_addr = "node1"
remote_addr = "node2"
os_swap = True
device_name = "nvme0n1"
partition_name = "nvme0n1p4"
read_ahead_kb = 4
do_drop_cache = True
do_block_tracing = False

######################
# BOTH Elastic and Vacuum
######################
n_server_threads = [25]
n_client_threads = [128] # client
# mem_size_list = [16*GB, 4*GB, 2*GB, 1*GB, 900*MB, 800*MB, 700*MB]
# mem_size_list = [900*MB, 880*MB, 860*MB, 840*MB, 820*MB]
mem_size_list = [815*MB, 810*MB, 805*MB]
mem_swappiness = 60
# query_paths = ["/mnt/ssd/querylog_no_repeated"]
# query_paths = ["/mnt/ssd/realistic_querylog"]
query_paths = ["/mnt/ssd/by-doc-freq/unique_terms_1e2"]
# query_paths = ["/mnt/ssd/querylog_no_repeated.rand"]
# query_paths = glob.glob("/mnt/ssd/split-log/*")



######################
# Vacuum only
######################
search_engine = "vacuum:vacuum_dump:/mnt/ssd/vacuum-files-little-packed"
profile_qq_server = "false"
lock_memory = "true"


######################
# Elastic only
######################
ELASTIC_DIR = "/users/jhe/elasticsearch-5.6.3"
lock_es_memory = ["true"]
init_heap_size = [512*MB]
max_heap_size = [512*MB]




if device_name == "sdc":
    partition_padding_bytes = 0
elif device_name == "nvme0n1":
    partition_padding_bytes = 46139392 * 512

gprof_env = os.environ.copy()
gprof_env["CPUPROFILE_FREQUENCY"] = '1000'

class VacuumClientOutput:
    def _parse_perf(self, header, data_line):
        header = header.split()
        items = data_line.split()

        print header
        print items

        return dict(zip(header, items))

    def parse_client_out(self, path):
        with open(path) as f:
            lines = f.readlines()

        for i, line in enumerate(lines):
            if "latency_95th" in line:
                return self._parse_perf(lines[i], lines[i+1])


class ElasticClientOutput:
    def _parse_throughput(self, line):
        qps = line.split()[1]
        line = line.replace(",", " ")
        return {"QPS": qps.split(".")[0]}

    def _parse_latencies(self, line):
        line = line.replace(",", " ")
        items = line.split()
        return {
                "latency_50th": items[1],
                "latency_95th": items[2],
                "latency_99th": items[3]}

    def parse_client_out(self, path):
        with open(path) as f:
            lines = f.readlines()

        d = {}
        for i, line in enumerate(lines):
            print line
            if line.startswith("Throughput:"):
                d.update(self._parse_throughput(lines[i]))
            elif line.startswith("Latencies:"):
                d.update(self._parse_latencies(lines[i]))

        return d


class Cgroup(object):
    def __init__(self, name, subs):
        self.name = name
        self.subs = subs

        shcmd('sudo cgcreate -g {subs}:{name}'.format(subs=subs, name=name))

    def set_item(self, sub, item, value):
        """
        Example: sub='memory', item='memory.limit_in_bytes'
        echo 3221225472 > memory/confine/memory.limit_in_bytes
        """
        path = self._path(sub, item)
        shcmd("echo {value} |sudo tee {path}".format(value = value, path = path))

        # self._write(sub, item, value)

        ret_value = self._read(sub, item)

        if ret_value != str(value):
            print 'Warning:', ret_value, '!=', value

    def get_item(self, sub, item):
        return self._read(sub, item)

    def execute(self, cmd):
        cg_cmd = ['sudo', 'cgexec',
                '-g', '{subs}:{name}'.format(subs=self.subs, name=self.name),
                '--sticky']
        cg_cmd += cmd
        print cg_cmd
        p = subprocess.Popen(cg_cmd, preexec_fn=os.setsid)

        return p

    def _write(self, sub, item, value):
        path = self._path(sub, item)
        with open(path, 'w') as f:
            f.write(str(value))

    def _read(self, sub, item):
        path = self._path(sub, item)
        with open(path, 'r') as f:
            value = f.read()

        return value.strip()

    def _path(self, sub, item):
        path = os.path.join('/sys/fs/cgroup', sub, self.name, item)
        return path


def get_cgroup_page_cache_size():
    def parse_page_cache(line):
        cache_size = line.split()[2]
        d = {"page_cache_size": cache_size}
        return d

    lines = subprocess.check_output(
            "cgget -g memory:/charlie", shell=True).split('\n')

    d = {}
    for i, line in enumerate(lines):
        if line.startswith("memory.stat: cache"):
            d.update(parse_page_cache(lines[i]))

    return int(d['page_cache_size'])


def set_es_yml(conf):
    with open(os.path.join(
        ELASTIC_DIR, "config/elasticsearch.yml.template"), "r") as f:
        lines = f.readlines()

    new_lines = []
    for line in lines:
        if "thread_pool.search.size" in line:
            new_lines.append(
                "thread_pool.search.size: {}\n".format(conf['n_server_threads']))
        elif "bootstrap.memory_lock" in line:
            new_lines.append(
                "bootstrap.memory_lock: {}\n".format(conf['lock_es_memory']))
        else:
            new_lines.append(line)

    with open(os.path.join(ELASTIC_DIR, "config/elasticsearch.yml"), "w") as f:
        f.write("".join(new_lines))

def set_jvm_options(conf):
    with open(os.path.join(ELASTIC_DIR, "config/jvm.options.template"), "r") as f:
        lines = f.readlines()

    new_lines = []
    for line in lines:
        if "init_heap_size" in line:
            new_lines.append(
                "-Xms{}\n".format(conf['init_heap_size']))
        elif "max_heap_size" in line:
            new_lines.append(
                "-Xmx{}\n".format(conf['max_heap_size']))
        else:
            new_lines.append(line)

    with open(os.path.join(ELASTIC_DIR, "config/jvm.options"), "w") as f:
        f.write("".join(new_lines))


def log_crashed_server(conf):
    with open("server.log", "a") as f:
        f.write("server crashed at " + now() + " ")
        f.write(repr(conf) + "\n")

def send_sigint(pid):
    shcmd("bash -c 'sudo kill -SIGINT {}'".format(os.getpgid(pid)))
    # os.killpg(os.getpgid(pid), signal.SIGINT)

def get_iostat_mb_read():
    out = subprocess.check_output("iostat -m {}".format(partition_name), shell=True)
    print out
    return parse_iostat(out)['io']['MB_read']

def create_blktrace_manager(subexpdir):
    return BlockTraceManager(
            dev = os.path.join("/dev", partition_name),
            event_file_column_names = ['pid', 'operation', 'offset', 'size',
                'timestamp', 'pre_wait_time'],
            resultpath = os.path.join(subexpdir, "raw-trace.txt"),
            to_ftlsim_path = os.path.join(subexpdir, "trace-table.txt"),
            sector_size = 512,
            padding_bytes = partition_padding_bytes,
            do_sort = False)

def kill_blktrace():
    shcmd("sudo pkill blktrace", ignore_error = True)

def drop_cache():
    shcmd("sudo dropcache")

def set_swap(val):
    if val == True:
        print "-" * 20
        print "Enabling swap..."
        print "-" * 20
        shcmd("sudo swapon -a")
    else:
        print "-" * 20
        print "Disabling swap..."
        print "-" * 20
        shcmd("sudo swapoff -a")

def set_read_ahead_kb(val):
    path = "/sys/block/{}/queue/read_ahead_kb".format(device_name)
    shcmd("echo {} |sudo tee {}".format(val, path))

def check_vacuum_port():
    print "-" * 20
    print "Port of qq server"
    shcmd("sudo netstat -ap | grep qq_server")
    print "-" * 20


def remote_cmd_chwd(dir_path, cmd, fd = None):
    return remote_cmd("cd {}; {}".format(dir_path, cmd), fd)

def remote_cmd(cmd, fd = None):
    print "Starting command on remote node.... ", cmd
    if fd is None:
        p = subprocess.Popen(["ssh", remote_addr, cmd])
    else:
        p = subprocess.Popen(["ssh", remote_addr, cmd], stdout = fd, stderr = fd)

    return p

def sync_build_dir():
    if server_addr == remote_addr:
        return

    build_path = "/users/jhe/flashsearch/src/qq_mem/build/engine_bench"
    shcmd("rsync -avzh --progress {path} {addr}:{path}".format(
        addr=remote_addr, path=build_path))

def sync_query_log(path):
    if server_addr == remote_addr:
        return

    shcmd("rsync -avzh --progress {path} {addr}:{path}".format(
        addr=remote_addr, path=path))

def compile_engine_bench():
    shcmd("make engine_bench_build")

def start_client(n_threads, query_path):
    print "-" * 20
    print "starting client ..."
    print "-" * 20
    return remote_cmd_chwd(
        "/users/jhe/flashsearch/src/qq_mem/build/",
        "./engine_bench -exp_mode=grpclog -n_threads={n_threads} "
        "-grpc_server={server} -query_path={query_path}"
        .format(server = server_addr, n_threads = n_threads,
           query_path = query_path),
        open("/tmp/client.out", "w")
        )

def print_client_output_tail():
    out = subprocess.check_output("tail -n 2 /tmp/client.out", shell=True)
    print out

def is_client_finished():
    out = subprocess.check_output("tail -n 1 /tmp/client.out", shell=True)
    print "check out: ", out
    if "ExperimentFinished!!!" in out:
        return True
    else:
        return False

def median(lst):
    sortedLst = sorted(lst)
    lstLen = len(lst)
    index = (lstLen - 1) // 2

    if (lstLen % 2):
        return sortedLst[index]
    else:
        return (sortedLst[index] + sortedLst[index + 1])/2.0


def kill_client():
    p = remote_cmd("sudo pkill RediSearchBench")
    p.wait()
    p = remote_cmd("sudo pkill engine_bench")
    p.wait()


def kill_server():
    shcmd("sudo pkill qq_server", ignore_error=True)
    shcmd("sudo pkill java", ignore_error=True)

def kill_subprocess(p):
    # os.killpg(os.getpgid(p.pid), signal.SIGTERM)
    p.kill()
    p.wait()

def copy_client_out():
    prepare_dir("/tmp/results")
    shcmd("cp /tmp/client.out /tmp/results/{}".format(now()))

def start_server(conf):
    print "-" * 20
    print "starting server ..."
    print "server mem size:", conf['server_mem_size']
    print "-" * 20

    cg = Cgroup(name='charlie', subs='memory')
    cg.set_item('memory', 'memory.limit_in_bytes', conf['server_mem_size'])
    cg.set_item('memory', 'memory.swappiness', mem_swappiness)

    with cd("/users/jhe/flashsearch/src/qq_mem"):
        cmd = "./build/qq_server "\
              "-sync_type=ASYNC -n_threads={n_threads} "\
              "-addr={server} -port=50051 -engine={engine} -profile_vacuum={profile} "\
              "-lock_memory={lock_mem}"\
              .format(server = server_addr,
                    n_threads = conf['n_server_threads'],
                    engine = search_engine,
                    profile = profile_qq_server,
                    lock_mem = lock_memory)
        print "-" * 20
        print "server cmd:", cmd
        print "-" * 20
        # p = subprocess.Popen(shlex.split(cmd), env = gprof_env)
        p = cg.execute(shlex.split(cmd))

        time.sleep(2)

        return p


class Exp(Experiment):
    def __init__(self):
        self._exp_name = "exp-" + now()

        self.confs = parameter_combinations({
                "server_mem_size": mem_size_list,
                "n_server_threads": n_server_threads,
                "n_client_threads": n_client_threads,
                "query_path": query_paths
                })
        self._n_treatments = len(self.confs)

        self.result = []

    def conf(self, i):

        return self.confs[i]

    def before(self):
        sync_build_dir()

    def beforeEach(self, conf):
        kill_server()
        kill_blktrace()

        kill_client()
        time.sleep(1)
        shcmd("rm -f /tmp/client.out")

        set_swap(os_swap)
        set_read_ahead_kb(read_ahead_kb)
        if do_drop_cache == True:
            drop_cache()

        sync_query_log(conf['query_path'])

    def treatment(self, conf):
        print conf
        server_p = start_server(conf)

        print "Wating for some time util the server starts...."
        time.sleep(10)
        mb_read_a = get_iostat_mb_read()

        check_vacuum_port()

        # Start block tracing after the server is fully loaded
        if do_block_tracing is True:
            self.blocktracer = create_blktrace_manager(self._subexpdir)
            self.blocktracer.start_tracing_and_collecting(['issue'])
        print "Wait for client to fully start (1 sec)..."
        time.sleep(1)

        client_p = start_client(conf['n_client_threads'],
                                conf['query_path'])

        seconds = 0
	cache_size_log = []
        while True:
            print_client_output_tail()
            finished = is_client_finished()
            print "is_client_finished()", finished
            print conf

            is_server_running = is_command_running("./build/qq_server")
            if is_server_running is False:
                # raise RuntimeError("Server just crashed!!!")
                log_crashed_server(conf)
                return

            if finished:
                kill_client()
                # kill_server()
                send_sigint(server_p.pid)
                print "Waiting for server to exit gracefuly"
                time.sleep(8)
                copy_client_out()
                break

            cache_size = get_cgroup_page_cache_size()/MB
            print "*" * 20
            print "page cache size (MB): ", cache_size
            print "*" * 20
            cache_size_log.append(cache_size)


            time.sleep(1)
            seconds += 1
            print ">>>>> It has been", seconds, "seconds <<<<<<"
            print "cache_size_log:", cache_size_log

        mb_read_b = get_iostat_mb_read()

        mb_read = int(mb_read_b) - int(mb_read_a)
        print '-' * 30
        print "MB read: ", mb_read
        print '-' * 30

        d = VacuumClientOutput().parse_client_out("/tmp/client.out")
        d["p_cache_median"] = median(cache_size_log)
        d["p_cache_max"] = max(cache_size_log)
        d['MB_read'] = mb_read
        d.update(conf)

        self.result.append(d)

    def afterEach(self, conf):
        path = os.path.join(self._resultdir, "result.txt")
        table_to_file(self.result, path, width = 20)
        shcmd("cat " + path)

        if do_block_tracing is True:
            shcmd("sync")
            self.blocktracer.stop_tracing_and_collecting()
            time.sleep(2)
            self.blocktracer.create_event_file_from_blkparse()

if __name__ == "__main__":
    exp = Exp()
    exp.main()

    # p = start_server({'server_mem_size': 10*GB})
    # print 'pid:', p.pid
    # time.sleep(15)
    # send_sigint(p.pid)

    # p.wait()
