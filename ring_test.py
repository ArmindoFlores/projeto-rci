import argparse
import select
import socket
import subprocess
import time

import colorama


class TestResult:
    def __init__(self, success, error_message="", program_output="", can_error=True):
        self._success = success
        self._error_message = error_message
        self._program_output = program_output
        self._can_error = can_error

    @property
    def success(self):
        return self._success

    @property
    def error_message(self):
        return self._error_message

    @property
    def program_output(self):
        return self._program_output if not self.success else ""

    @property
    def can_error(self):
        return self._can_error

def cond_print(*args, verbosity=0, level=0, **kwargs):
    if verbosity <= level:
        print(*args, **kwargs)

def recv_message(s, timeout):
    if not hasattr(recv_message, "__messages"):
        recv_message.__messages = {}

    if s not in recv_message.__messages:
        recv_message.__messages[s] = ""

    while not "\n" in recv_message.__messages[s]:
        ready = select.select([s], [], [], timeout)
        if not ready[0]:
            return ""
        data = s.recv(2048).decode()
        if len(data) == 0:
            return None
        recv_message.__messages[s] += data

    pos = recv_message.__messages[s].index("\n")
    result = recv_message.__messages[s][:(pos+1)]
    recv_message.__messages[s] = recv_message.__messages[s][(pos+1):]
    return result

def frmt_message(msg):
    return "".join([char if char != "\n" else "\\n" for char in msg])

def create_process(executable, key, ip, port):
    return subprocess.Popen(
        [str(args.executable), str(key), str(ip), str(port)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        bufsize=1,
        encoding="utf-8"
    )

def create_tcp_server_socket(ip, port):
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((ip, port))
    server_socket.listen()
    return server_socket

def create_tcp_client_socket(ip, port):
    try:
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.connect((ip, port))
        return client_socket
    except ConnectionRefusedError:
        return None

def terminate_processes(procs):
    for proc in procs:
        proc.terminate()
        proc.stdout.flush()

def read_processes(procs):
    result = ""
    for i, proc in enumerate(procs):
        result += f"PROC{i+1}:\n{proc.stdout.read()}\n\n"
    return result

def verify_self_message(msg, processes, test_key, test_ip, test_port):
    if msg == "":
        terminate_processes(processes)
        return TestResult(False, "Sent 'SELF' message, but node did not respond (did you forget to terminate your message with '\\n'?)", read_processes(processes))

    try:
        name, key, ip, port = msg[:-1].split(" ")
    except (TypeError, ValueError):
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received ('{msg[:-1]}\\n' should be 'SELF {test_key} {test_ip} {test_port}\\n')", read_processes(processes))

    if name != "SELF":
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received (message type was '{name}' should be 'SELF')", read_processes(processes))

    if key != str(test_key):
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received (key parameter '{key}' should be '{test_key}')", read_processes(processes))

    if ip != test_ip:
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received (ip parameter '{ip}' should be '{test_ip}')", read_processes(processes))

    if port != str(test_port):
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received (port parameter '{port}' should be '{test_port}')", read_processes(processes))
    return TestResult(True)

def verify_pred_message(msg, processes, test_key, test_ip, test_port):
    if msg is None:
        return TestResult(False, f"Node disconnected before sending a message", read_processes(processes))
    try:
        name, key, ip, port = msg[:-1].split(" ")
    except (TypeError, ValueError):
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received ('{msg[:-1]}\\n' should be 'PRED {test_key} {test_ip} {test_port}\\n')", read_processes(processes))

    if name != "PRED":
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received (message type was '{name}' should be 'PRED')", read_processes(processes))

    if key != str(test_key):
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received (key parameter '{key}' should be '{test_key}')", read_processes(processes))

    if ip != test_ip:
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received (ip parameter '{ip}' should be '{test_ip}')", read_processes(processes))

    if port != str(test_port):
        terminate_processes(processes)
        return TestResult(False, f"Invalid response received (port parameter '{port}' should be '{test_port}')", read_processes(processes))
    return TestResult(True)

def test_create_ring(args):
    test_key = 1
    test_ip = "127.0.0.1"
    test_port = args.port + 1
    self_ip = "127.0.0.1"
    self_port = args.port + 2
    self_key = 2
    args.port += 2
    node_process = create_process(args.executable, test_key, test_ip, test_port)

    node_process.stdin.write("new\n")
    node_process.stdin.flush()

    time.sleep(.1)

    server_socket = create_tcp_server_socket(self_ip, self_port)

    client_socket = create_tcp_client_socket(test_ip, test_port)
    if client_socket is None:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Node did not start listening for connections", read_processes([node_process]))
    client_socket.sendall(bytes(f"SELF {self_key} {self_ip} {self_port}\n", "utf-8"))
    
    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Sent 'SELF' message, node did not connect back", read_processes([node_process]))

    conn_socket, _ = server_socket.accept()
    message = recv_message(conn_socket, 1)
   
    result = verify_self_message(message, [node_process], test_key, test_ip, test_port)
    if not result.success:
        conn_socket.close()
        server_socket.close()
        return result

    terminate_processes([node_process])
    conn_socket.close()
    server_socket.close()
    return TestResult(True)

def test_join_ring(args):
    test_key = 1
    test_ip = "127.0.0.1"
    test_port = args.port + 1
    self_ip = "127.0.0.1"
    self_port = args.port + 2
    self_key = 2
    args.port += 2

    node_process = create_process(args.executable, test_key, test_ip, test_port)

    server_socket = create_tcp_server_socket(self_ip, self_port)
    
    node_process.stdin.write(f"pentry {self_key} {self_ip} {self_port}\n")
    node_process.stdin.flush()

    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Sent 'pentry' command, node did not connect", read_processes([node_process]))

    conn_socket, _ = server_socket.accept()
    message = recv_message(conn_socket, 1)

    result = verify_self_message(message, [node_process], test_key, test_ip, test_port)
    if not result.success:
        conn_socket.close()
        server_socket.close()
        return result

    terminate_processes([node_process])
    conn_socket.close()
    server_socket.close()
    return TestResult(True)

def test_three_node_ring(args):
    test1_key = 2
    test1_ip = "127.0.0.1"
    test1_port = args.port + 2
    test2_key = 3
    test2_ip = "127.0.0.1"
    test2_port = args.port + 3
    self_ip = "127.0.0.1"
    self_port = args.port + 1
    self_key = 1
    args.port += 3

    node1_process = create_process(args.executable, test1_key, test1_ip, test1_port)
    node2_process = create_process(args.executable, test2_key, test2_ip, test2_port)

    server_socket = create_tcp_server_socket(self_ip, self_port)
    
    node1_process.stdin.write(f"pentry {self_key} {self_ip} {self_port}\n")
    node1_process.stdin.flush()

    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node1_process, node2_process])
        server_socket.close()
        return TestResult(False, "Sent 'pentry' command, node did not connect", read_processes([node1_process, node2_process]))

    conn1_socket, _ = server_socket.accept()
    message = recv_message(conn1_socket, 1)
    
    result = verify_self_message(message, [node1_process, node2_process], test1_key, test1_ip, test1_port)

    if not result.success:
        conn1_socket.close()
        server_socket.close()
        return result

    client_socket = create_tcp_client_socket(test1_ip, test1_port)
    if client_socket is None:
        terminate_processes([node1_process, node2_process])
        server_socket.close()
        return TestResult(False, "Node did not start listening for connections", read_processes([node1_process, node2_process]))
    client_socket.sendall(bytes(f"SELF {self_key} {self_ip} {self_port}\n", "utf-8"))

    node2_process.stdin.write(f"pentry {test1_key} {test1_ip} {test1_port}\n")
    node2_process.stdin.flush()

    message = recv_message(client_socket, 1)
    
    result = verify_pred_message(message, [node1_process, node2_process], test2_key, test2_ip, test2_port)
    if not result.success:
        conn1_socket.close()
        server_socket.close()
        return result

    terminate_processes([node1_process, node2_process])
    conn1_socket.close()
    server_socket.close()
    return TestResult(True)

def test_three_node_ring_enter(args):
    test1_key = 1
    test1_ip = "127.0.0.1"
    test1_port = args.port + 1
    test2_key = 2
    test2_ip = "127.0.0.1"
    test2_port = args.port + 2
    self_ip = "127.0.0.1"
    self_port = args.port + 3
    self_key = 3
    args.port += 3

    node1_process = create_process(args.executable, test1_key, test1_ip, test1_port)
    node2_process = create_process(args.executable, test2_key, test2_ip, test2_port)

    node1_process.stdin.write(f"new\n")
    node1_process.stdin.flush()

    time.sleep(.1)

    node2_process.stdin.write(f"pentry {test1_key} {test1_ip} {test1_port}\n")
    node2_process.stdin.flush()

    time.sleep(.1)

    if node1_process.poll() is not None or node2_process.poll() is not None:
        terminate_processes([node1_process, node2_process])
        return TestResult(False, "One or more nodes exited before finishing test execution", read_processes([node1_process, node2_process]))

    server_socket = create_tcp_server_socket(self_ip, self_port)

    client_socket = create_tcp_client_socket(test2_ip, test2_port)
    if client_socket is None:
        terminate_processes([node1_process, node2_process])
        server_socket.close()
        return TestResult(False, "Node did not start listening for connections", read_processes([node1_process, node2_process]))
    client_socket.sendall(bytes(f"SELF {self_key} {self_ip} {self_port}\n", "utf-8"))

    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node1_process, node2_process])
        server_socket.close()
        return TestResult(False, "Sent connection request, no node connected back", read_processes([node1_process, node2_process]))

    conn_socket, _ = server_socket.accept()
    message = recv_message(conn_socket, 1)

    result = verify_self_message(message, [node1_process, node2_process], test1_key, test1_ip, test1_port)
    if not result.success:
        conn_socket.close()
        server_socket.close()
        return result

    terminate_processes([node1_process, node2_process])
    conn_socket.close()
    server_socket.close()
    return TestResult(True)

def test_slow_messages(args):
    test_key = 1
    test_ip = "127.0.0.1"
    test_port = args.port + 1
    self_key = 2
    self_ip = "127.0.0.1"
    self_port = args.port + 2
    args.port += 3

    node_process = create_process(args.executable, test_key, test_ip, test_port)

    node_process.stdin.write(f"new\n")
    node_process.stdin.flush()

    time.sleep(.1)

    server_socket = create_tcp_server_socket(self_ip, self_port)
    
    client_socket = create_tcp_client_socket(test_ip, test_port)
    if client_socket is None:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Node did not start listening for connections", read_processes([node_process]))
    client_socket.sendall(b"SELF")
    time.sleep(.01)
    client_socket.sendall(bytes(f" {self_key}", "utf-8"))
    time.sleep(.01)
    client_socket.sendall(bytes(f" {self_ip}", "utf-8"))
    time.sleep(.01)
    client_socket.sendall(bytes(f" {self_port}\n", "utf-8"))

    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Sent 'SELF' message, node did not connect back", read_processes([node_process]))

    conn_socket, _ = server_socket.accept()
    message = recv_message(conn_socket, 1)
    
    result = verify_self_message(message, [node_process], test_key, test_ip, test_port)
    if not result.success:
        conn_socket.close()
        server_socket.close()
        return result

    terminate_processes([node_process])
    conn_socket.close()
    server_socket.close()
    return TestResult(True)

def test_invalid_message(args):
    test_key = 1
    test_ip = "127.0.0.1"
    test_port = args.port + 1
    args.port += 1

    node_process = create_process(args.executable, test_key, test_ip, test_port)

    node_process.stdin.write(f"new\n")
    node_process.stdin.flush()

    time.sleep(.1)
    
    client_socket = create_tcp_client_socket(test_ip, test_port)
    if client_socket is None:
        terminate_processes([node_process])
        return TestResult(False, "Node did not start listening for connections", read_processes([node_process]))
    client_socket.sendall(b"SELF xxxx invalid\n")

    time.sleep(.1)

    if node_process.poll() is not None:
        return TestResult(False, "Node crashed when faced with an invalid message", read_processes([node_process]))

    message = recv_message(client_socket, 1)
    if message == "":
        terminate_processes([node_process])
        return TestResult(False, "Node does not disconnect when faced with an invalid message", can_error=False)
    if message is None:
        terminate_processes([node_process])
        return TestResult(True, "Node disconnects when faced with an invalid message", can_error=False)

    terminate_processes([node_process])
    return TestResult(False, f"Node sent back '{frmt_message(message)}' when faced with an invalid message", read_processes([node_process]))

def test_big_message(args):
    test_key = 1
    test_ip = "127.0.0.1"
    test_port = args.port + 1
    args.port += 1

    node_process = create_process(args.executable, test_key, test_ip, test_port)

    node_process.stdin.write(f"new\n")
    node_process.stdin.flush()

    time.sleep(.1)
    
    client_socket = create_tcp_client_socket(test_ip, test_port)
    if client_socket is None:
        terminate_processes([node_process])
        return TestResult(False, "Node did not start listening for connections", read_processes([node_process]))
    client_socket.sendall(b"abcd"*100000+b"\n")

    time.sleep(.1)

    if node_process.poll() is not None:
        return TestResult(False, "Node crashed when faced with a big message", read_processes([node_process]))

    terminate_processes([node_process])
    return TestResult(True)

def test_two_node_other_ring_leave(args):
    test_key = 1
    test_ip = "127.0.0.1"
    test_port = args.port + 1
    self_ip = "127.0.0.1"
    self_port = args.port + 2
    self_key = 2
    args.port += 2

    node_process = create_process(args.executable, test_key, test_ip, test_port)

    node_process.stdin.write("new\n")
    node_process.stdin.flush()

    time.sleep(.1)

    server_socket = create_tcp_server_socket(self_ip, self_port)

    client_socket = create_tcp_client_socket(test_ip, test_port)
    if client_socket is None:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Node did not start listening for connections", read_processes([node_process]))
    client_socket.sendall(bytes(f"SELF {self_key} {self_ip} {self_port}\n", "utf-8"))
    
    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Sent 'SELF' message, node did not connect back", read_processes([node_process]))

    conn_socket, _ = server_socket.accept()
    message = recv_message(conn_socket, 1)
   
    result = verify_self_message(message, [node_process], test_key, test_ip, test_port)
    if not result.success:
        conn_socket.close()
        server_socket.close()
        return result

    conn_socket.sendall(bytes(f"PRED {test_key} {test_ip} {test_port}\n", "utf-8"))
    conn_socket.close()
    server_socket.close()
    client_socket.close()

    time.sleep(.1)

    if node_process.poll() is not None:
        return TestResult(False, "Node crashed after another node left the ring", read_processes([node_process]))

    terminate_processes([node_process])
    return TestResult(True)

def test_two_node_ring_leave(args):
    test_key = 1
    test_ip = "127.0.0.1"
    test_port = args.port + 1
    self_ip = "127.0.0.1"
    self_port = args.port + 2
    self_key = 2
    args.port += 2

    node_process = create_process(args.executable, test_key, test_ip, test_port)

    node_process.stdin.write("new\n")
    node_process.stdin.flush()

    time.sleep(.1)

    server_socket = create_tcp_server_socket(self_ip, self_port)

    client_socket = create_tcp_client_socket(test_ip, test_port)
    if client_socket is None:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Node did not start listening for connections", read_processes([node_process]))
    client_socket.sendall(bytes(f"SELF {self_key} {self_ip} {self_port}\n", "utf-8"))
    
    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node_process])
        server_socket.close()
        return TestResult(False, "Sent 'SELF' message, node did not connect back", read_processes([node_process]))

    conn_socket, _ = server_socket.accept()
    message = recv_message(conn_socket, 1)
   
    result = verify_self_message(message, [node_process], test_key, test_ip, test_port)
    if not result.success:
        conn_socket.close()
        server_socket.close()
        return result

    node_process.stdin.write("leave\n")
    node_process.stdin.flush()

    message = recv_message(client_socket, 1)
    result = verify_pred_message(message, [node_process], self_key, self_ip, self_port)
    if not result.success:
        conn_socket.close()
        server_socket.close()
        return result

    terminate_processes([node_process])
    conn_socket.close()
    server_socket.close()
    return TestResult(True)

def test_three_node_ring_leave(args):
    test1_key = 2
    test1_ip = "127.0.0.1"
    test1_port = args.port + 2
    test2_key = 3
    test2_ip = "127.0.0.1"
    test2_port = args.port + 3
    self_ip = "127.0.0.1"
    self_port = args.port + 1
    self_key = 1
    args.port += 3

    node1_process = create_process(args.executable, test1_key, test1_ip, test1_port)
    node2_process = create_process(args.executable, test2_key, test2_ip, test2_port)

    server_socket = create_tcp_server_socket(self_ip, self_port)
    
    node1_process.stdin.write(f"pentry {self_key} {self_ip} {self_port}\n")
    node1_process.stdin.flush()

    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node1_process, node2_process])
        server_socket.close()
        return TestResult(False, "Sent 'pentry' command, node did not connect", read_processes([node1_process, node2_process]))

    conn1_socket, _ = server_socket.accept()
    message = recv_message(conn1_socket, 1)
    
    result = verify_self_message(message, [node1_process, node2_process], test1_key, test1_ip, test1_port)

    if not result.success:
        conn1_socket.close()
        server_socket.close()
        return result

    client_socket = create_tcp_client_socket(test1_ip, test1_port)
    if client_socket is None:
        terminate_processes([node1_process, node2_process])
        server_socket.close()
        return TestResult(False, "Node did not start listening for connections", read_processes([node1_process, node2_process]))
    client_socket.sendall(bytes(f"SELF {self_key} {self_ip} {self_port}\n", "utf-8"))

    node2_process.stdin.write(f"pentry {test1_key} {test1_ip} {test1_port}\n")
    node2_process.stdin.flush()

    message = recv_message(client_socket, 1)
    
    result = verify_pred_message(message, [node1_process, node2_process], test2_key, test2_ip, test2_port)
    if not result.success:
        conn1_socket.close()
        server_socket.close()
        return result

    node1_process.stdin.write("leave\n")
    node1_process.stdin.flush()

    ready = select.select([server_socket], [], [], 1)
    if not ready[0]:
        terminate_processes([node1_process, node2_process])
        conn1_socket.close()
        server_socket.close()
        return TestResult(False, "Sent 'leave' command, remaining node did not connect", read_processes([node1_process, node2_process]))

    conn2_socket, _ = server_socket.accept()
    message = recv_message(conn2_socket, 1)
    result = verify_self_message(message, [node1_process, node2_process], test2_key, test2_ip, test2_port)
    if not result.success:
        conn1_socket.close()
        conn2_socket.close()
        server_socket.close()
        return result

    terminate_processes([node1_process, node2_process])
    conn1_socket.close()
    conn2_socket.close()
    server_socket.close()
    return TestResult(True)

TESTS = {
    "TWO_NODE_CREATE_RING": (test_create_ring, None),
    "TWO_NODE_JOIN_RING": (test_join_ring, None),
    "THREE_NODE_RING": (test_three_node_ring, None),
    "THREE_NODE_RING_ENTER": (test_three_node_ring_enter, None),
    "TWO_NODE_RING_LEAVE": (test_two_node_ring_leave, None),
    "TWO_NODE_OTHER_RING_LEAVE": (test_two_node_other_ring_leave, None),
    "THREE_NODE_RING_LEAVE": (test_three_node_ring_leave, None),
    "BIG_MESSAGE": (test_big_message, None),
    "SLOW_MESSAGES": (test_slow_messages, None),
    "INVALID_MESSAGE": (test_invalid_message, None),
}

def build_parser():
    parser = argparse.ArgumentParser(description="Automated test suite for the 2021/2022 RCI project")
    parser.add_argument("executable", help="The name of the args.executable to test")
    parser.add_argument("-v", "--verbose", action="count", default=0, help="Change this program's verbosity")
    parser.add_argument("-p", "--port", type=int, default=36000, help="Starting port number. Defaults to 36000")
    return parser

def main(args):
    errors = 0
    for test_name, (test, _) in TESTS.items():
        result = test(args)
        if result.success:
            if result.can_error:
                cond_print(f"{colorama.Fore.GREEN}[+] Completed {test_name} test!{colorama.Fore.RESET}", verbosity=0, level=args.verbose)
            else:
                cond_print(f"{colorama.Fore.GREEN}[+] {test_name}: {result.error_message}{colorama.Fore.RESET}")
        else:
            if result.can_error:
                errors += 1
                with open(f"{test_name}.txt", "w") as f:
                    f.write(result.program_output)
                cond_print(f"{colorama.Fore.RED}[!] Error in test {test_name}: {result.error_message}.", end=" ", verbosity=0, level=args.verbose)
                cond_print(f"Program output written to {test_name}.txt{colorama.Fore.RESET}", verbosity=0, level=args.verbose)
            else:
                cond_print(f"{colorama.Fore.YELLOW}[!] {test_name}: {result.error_message}{colorama.Fore.RESET}")
    if errors != 0 and args.verbose == 0:
        cond_print(f"For more information about the errors, re-run the command with -v", verbosity=0, level=args.verbose)


if __name__ == "__main__":
    parser = build_parser()
    args = parser.parse_args()
    main(args)
