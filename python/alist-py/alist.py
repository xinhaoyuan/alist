import re
import struct
from collections import deque

class ParseException(Exception):
    pass

class AListParser:

    STATE_ELEMENT_START    = 0
    STATE_ELEMENT_END      = 1
    STATE_ALIST            = 2
    STATE_ALIST_WITH_KEY   = 3
    STATE_SQ_STRING        = 4
    STATE_DQ_STRING        = 5
    STATE_SML_STRING       = 6
    STATE_DML_STRING       = 7

    # STATE_ELEMENT_START    = "ES"
    # STATE_ELEMENT_END      = "EE"
    # STATE_ALIST            = "AL"
    # STATE_ALIST_WITH_KEY   = "ALK"
    # STATE_SQ_STRING        = "SQ"
    # STATE_DQ_STRING        = "DQ"
    # STATE_SML_STRING       = "SML"
    # STATE_DML_STRING       = "DML"

    COMMENT_CHAR_RE = re.compile(r'#')
    SQ_STRING_SPECIAL_RE = re.compile(r'[\\\']')
    DQ_STRING_SPECIAL_RE = re.compile(r'[\\"]')
    SML_STRING_SPECIAL_RE = re.compile(r'\\|\'\'\'')
    DML_STRING_SPECIAL_RE = re.compile(r'\\|"""')
    UNQUOTED_RE = re.compile(r'[^\[\]="\', \t#]+')
    KEY_VALUE_SEP_RE = re.compile("=")
    ITEM_SEP_RE = re.compile(",")
    NON_WHITESPACE_RE = re.compile(r'[^ \t]')

    BUF_CLEAN_SIZE = 4096

    def __init__(self, multi = True):
        self.reset(multi)

    def reset(self, multi = True):
        self.sealed = False
        self.multi = multi
        self.state_stack = [ self.STATE_ELEMENT_START ];
        self.value_stack = [ None ];
        self.buf = ""
        self.buf_read_pos = 0
        self.values = deque()

    def seal(self):
        if self.sealed:
            return
        self.sealed = True
        self.state_stack = [ ];
        self.value_stack = [ ];
        self.buf = ""

    def state_get(self):
        if len(self.state_stack) == 0:
            raise ParseException("stack is empty")
        return self.state_stack[len(self.state_stack) - 1]

    def state_set(self, state):
        if len(self.state_stack) == 0:
            raise ParseException("stack is empty")
        self.state_stack[len(self.state_stack) - 1] = state

    def value_get(self):
        if len(self.value_stack) == 0:
            raise ParseException("stack is empty")
        return self.value_stack[len(self.value_stack) - 1]

    def value_set(self, value):
        if len(self.value_stack) == 0:
            raise ParseException("stack is empty")
        self.value_stack[len(self.value_stack) - 1] = value

    def pop(self):
        if len(self.state_stack) == 0:
            raise ParseException("stack is empty")
        self.state_stack.pop()
        self.value_stack.pop()

    def push(self, state, value):
        self.state_stack.append(state)
        self.value_stack.append(value)

    def parse(self, s):
        self.parse_lines(s.splitlines())
        return self.extract()

    def parse_lines(self, lines):
        for line in lines:
            self.parse_line(line)
        pass

    def parse_line(self, line):
        if self.sealed:
            return
        self.buf += line
        self.parse_buf()

    def handle_escape(self):
        start_c = self.buf[self.buf_read_pos]
        if start_c == 'n':
            self.value_get().extend(b'\n')
            self.buf_read_pos += 1
        elif start_c == 't':
            self.value_get().extend(b'\t')
            self.buf_read_pos += 1
        elif start_c == 'b':
            self.value_get().extend(b'\b')
            self.buf_read_pos += 1
        elif start_c == 'f':
            self.value_get().extend(b'\f')
            self.buf_read_pos += 1
        elif start_c == 'x':
            if self.buf_read_pos + 2 >= len(self.buf):
                raise ParseException("expect 2 hex chars for utf-8 escaping")
            code_str = self.buf[self.buf_read_pos + 1:self.buf_read_pos + 3]
            try:
                code = int(code_str, 16)
            except Exception:
                raise ParseException("expect 2 hex chars for utf-8 escaping (parse {0} failed)".format(code_str))
            self.value_get().append(code)
            self.buf_read_pos += 3
        elif start_c == '"' or start_c == "'" or start_c == '\\':
            self.value_get().append(ord(start_c))
            self.buf_read_pos += 1
        else:
            self.value_get().extend(b"\\")
            self.value_get().append(ord(start_c))

    def clean_buf(self):
        if self.buf_read_pos > len(self.buf):
            self.buf_read_pos = len(self.buf)
        if self.buf_read_pos > self.BUF_CLEAN_SIZE:
            self.buf = self.buf[self.buf_read_pos:]
            self.buf_read_pos = 0

    def parse_buf(self):
        state = None
        while len(self.buf) > self.buf_read_pos or state == self.STATE_ELEMENT_END:

            self.clean_buf()

            if len(self.state_stack) == 0:
                self.seal()
                return

            state = self.state_get()
            current = self.value_get()
            read_pos = self.buf_read_pos
            # print((state, current, read_pos))

            if state == self.STATE_ELEMENT_END:
                value = current
                if isinstance(value, bytearray):
                    value = value.decode("utf-8")
                self.pop()

                if len(self.state_stack) == 0:
                    self.values.append(value)

                    if self.multi:
                        self.push(self.STATE_ELEMENT_START, None)

                    continue

                state = self.state_get()
                current = self.value_get()

                if state == self.STATE_ALIST:
                    if current.l is not None and current.v is not None:
                        current.l.append(current.v)
                    else:
                        current.l = []
                    current.v = value
                elif state == self.STATE_ALIST_WITH_KEY:
                    if current.m is None:
                        current.m = {}
                    current.m[current.v] = value
                    current.v = None
                else:
                    raise ParseException("invalid position to insert element")

            elif state == self.STATE_SQ_STRING:
                m = self.SQ_STRING_SPECIAL_RE.search(self.buf, read_pos)
                if not m:
                    current.extend(self.buf[read_pos:].encode("utf-8"))
                    state = self.STATE_ELEMENT_END
                    read_pos = len(self.buf)
                else:
                    current.extend(self.buf[read_pos:m.start(0)].encode("utf-8"))
                    if self.buf[m.start(0)] == "\\":
                        self.buf_read_pos = m.start(0) + 1
                        self.handle_escape()
                        continue
                    elif self.buf[m.start(0)] == "'":
                        # the string is ended
                        read_pos = m.start(0) + 1
                        state = self.STATE_ELEMENT_END
                    else:
                        raise ParseException("format error in quoted string")

            elif state == self.STATE_DQ_STRING:
                m = self.DQ_STRING_SPECIAL_RE.search(self.buf, read_pos)
                if not m:
                    current.extend(self.buf[read_pos:].encode("utf-8"))
                    state = self.STATE_ELEMENT_END
                    read_pos = len(self.buf)
                else:
                    current.extend(self.buf[read_pos:m.start(0)].encode("utf-8"))
                    if self.buf[m.start(0)] == "\\":
                        self.buf_read_pos = m.start(0) + 1
                        self.handle_escape()
                        continue
                    elif self.buf[m.start(0)] == '"':
                        # the string is ended
                        read_pos = m.start(0) + 1
                        state = self.STATE_ELEMENT_END
                    else:
                        raise ParseException("format error in quoted string")

            elif state == self.STATE_SML_STRING:
                m = self.SML_STRING_SPECIAL_RE.search(self.buf, read_pos)
                if not m:
                    current.extend(self.buf[read_pos:].encode("utf-8"))
                    current.extend(b"\n")
                    read_pos = len(self.buf)
                else:
                    current.extend(self.buf[read_pos:m.start(0)].encode("utf-8"))
                    if self.buf[m.start(0)] == "\\":
                        self.buf_read_pos = m.start(0) + 1
                        self.handle_escape()
                        continue
                    elif self.buf[m.start(0):m.start(0) + 3] == "'''":
                        # the string is ended
                        read_pos = m.start(0) + 3
                        state = self.STATE_ELEMENT_END
                    else:
                        raise ParseException("format error in multi-line string")

            elif state == self.STATE_DML_STRING:
                m = self.DML_STRING_SPECIAL_RE.search(self.buf, read_pos)
                if not m:
                    current.extend(self.buf[read_pos:].encode("utf-8"))
                    current.extend(b"\n")
                    read_pos = len(self.buf)
                else:
                    current.extend(self.buf[read_pos:m.start(0)].encode("utf-8"))
                    if self.buf[m.start(0)] == "\\":
                        self.buf_read_pos = m.start(0) + 1
                        self.handle_escape()
                        continue
                    elif self.buf[m.start(0):m.start(0) + 3] == '"""':
                        # the string is ended
                        read_pos = m.start(0) + 3
                        state = self.STATE_ELEMENT_END
                    else:
                        raise ParseException("format error in multi-line string")

            elif state == self.STATE_ALIST:
                m = self.NON_WHITESPACE_RE.search(self.buf, read_pos)
                if not m:
                    read_pos = len(self.buf)
                elif self.buf[m.start(0)] == "]":
                    if current.v is not None:
                        if current.l is None:
                            current.l = []
                        current.l.append(current.v)
                        current.v = None
                    state = self.STATE_ELEMENT_END
                    read_pos = m.start(0) + 1
                elif self.buf[m.start(0)] == "=":
                    if current.v is None:
                        raise ParseException("missing key")
                    elif not isinstance(current.v, str):
                        raise ParseException("key is not a string")
                    else:
                        state = self.STATE_ALIST_WITH_KEY
                        read_pos = m.start(0) + 1
                elif self.ITEM_SEP_RE.match(self.buf[m.start(0)]):
                    self.buf_read_pos = m.start(0) + 1
                    self.push(self.STATE_ELEMENT_START, None)
                    continue
                elif self.buf[m.start(0)] == "#":
                    read_pos = len(self.buf)
                else:
                    self.buf_read_pos = m.start(0)
                    self.push(self.STATE_ELEMENT_START, None)
                    continue

            elif state == self.STATE_ALIST_WITH_KEY:
                m = self.NON_WHITESPACE_RE.search(self.buf, read_pos)
                if not m:
                    read_pos = len(self.buf)
                elif self.buf[m.start(0)] == "]" or self.buf[m.start(0)] == ",":
                    raise ParseException("premature alist end after '='")
                elif self.buf[m.start(0)] == "=":
                    raise ParseException("unexpected '='")
                elif self.buf[m.start(0)] == "#":
                    read_pos = len(self.buf)
                else:
                    self.buf_read_pos = m.start(0)
                    self.push(self.STATE_ELEMENT_START, None)
                    continue

            elif state == self.STATE_ELEMENT_START:
                m = self.NON_WHITESPACE_RE.search(self.buf, read_pos)
                if not m:
                    read_pos = len(self.buf)
                elif self.buf[m.start(0)] == "[":
                    current = object()
                    current.l = None
                    current.m = None
                    current.v = None
                    state = self.STATE_ALIST
                    read_pos = m.start(0) + 1
                elif self.buf[m.start(0)] == "'":
                    current = bytearray()
                    if self.buf[m.start(0):m.start(0) + 3] == "'''":
                        state = self.STATE_SML_STRING
                        read_pos = m.start(0) + 3
                    else:
                        state = self.STATE_SQ_STRING
                        read_pos = m.start(0) + 1
                elif self.buf[m.start(0)] == '"':
                    current = bytearray()
                    if self.buf[m.start(0):m.start(0) + 3] == '"""':
                        state = self.STATE_DML_STRING
                        read_pos = m.start(0) + 3
                    else:
                        state = self.STATE_DQ_STRING
                        read_pos = m.start(0) + 1
                elif self.buf[m.start(0)] == "#":
                    read_pos = len(self.buf)
                elif self.UNQUOTED_RE.match(self.buf[m.start(0)]):
                    m_end = self.UNQUOTED_RE.search(self.buf, m.start(0))
                    if not m_end:
                        current = self.buf[m.start(0):]
                        state = self.STATE_ELEMENT_END
                        read_pos = len(self.buf)
                    else:
                        current = self.buf[m_end.start(0):m_end.end(0)]
                        state = self.STATE_ELEMENT_END
                        read_pos = m_end.end(0)

                    if len(current) == 0:
                        raise ParseException("unexpect char at element start")

                else:
                    raise ParseException("format error in parsing general element")

            self.state_set(state)
            self.value_set(current)
            self.buf_read_pos = read_pos

        self.clean_buf()

    def extract(self):
        if len(self.values) == 0:
            return None
        else:
            return self.values.popleft()
