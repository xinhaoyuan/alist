import re
import struct
from collections import deque

class ParseException(Exception):
    pass

class IAListOperator:
    def __init__(self):
        pass

    def new_alist(self):
        raise Exception("Not implemented")

    def new_string(self):
        raise Exception("Not implemented")

    def new_literal(self, s):
        raise Exception("Not implemented")

    def append_string(self, o, s):
        raise Exception("Not implemented")

    def append_item(self, o, i):
        raise Exception("Not implemented")

    def append_kv(self, o, k, is_literal, v):
        raise Exception("Not implemented")

    def finalize_alist(self, o):
        raise Exception("Not implemented")

    def finalize_string(self, o):
        raise Exception("Not implemented")

class AListOperator(IAListOperator):
    def new_alist(self):
        return { "l" : [], "m" : {} }

    def new_string(self):
        return bytearray()

    def new_literal(self, s):
        return s

    def append_string_bytearray(self, o, ba):
        o.extend(ba)
        return o

    def append_string_byte(self, o, b):
        o.append(b)
        return o

    def append_item(self, o, i):
        o["l"].append(i)
        return o

    def append_kv(self, o, k, is_literal, v):
        o["m"][k] = v
        return o

    def finalize_alist(self, o):
        if len(o["l"]) == 0:
            return o["m"]
        elif len(o["m"]) == 0:
            return o["l"]
        else:
            o["l"].append(o["m"])
            return o["l"]

    def finalize_string(self, o):
        return o.decode("utf-8")

class AListInternalValue:
    def __init__(self):
        self.has_tmp = False
        self.tmp = None
        self.o = None
        self.is_string = False
        self.is_literal = False

    def __repr__(self):
        return "[{0}|{1}]".format(self.o, self.tmp)

class AListParser:

    STATE_ELEMENT_START    = 0
    STATE_ELEMENT_END      = 1
    STATE_ALIST            = 2
    STATE_ALIST_WITH_KEY   = 3
    STATE_QUOTED_STRING    = 4
    STATE_MULTILINE_STRING = 5

    # STATE_ELEMENT_START    = "ES"
    # STATE_ELEMENT_END      = "EE"
    # STATE_ALIST            = "AL"
    # STATE_ALIST_WITH_KEY   = "AK"
    # STATE_QUOTED_STRING    = "QS"
    # STATE_MULTILINE_STRING = "MS"

    BUF_CLEAN_SIZE = 4096

    def __init__(self, multi = True):
        self.reset(multi,
                   AListOperator(),
                   "#", ",", ":=", "'\"", "[{", "]}")

    def reset(self, multi, op,
              c_line_comment, c_item_sep, c_kv_sep, c_quote, c_open, c_close):
        self.sealed = False
        self.multi = multi
        self.state_stack = [ self.STATE_ELEMENT_START ]
        self.s_aux_stack = [ None ]
        self.value_stack = [ None ]
        self.buf = ""
        self.buf_read_pos = 0
        self.values = deque()
        self.op = op
        self.c_line_comment = c_line_comment
        self.c_item_sep = c_item_sep
        self.c_kv_sep = c_kv_sep
        self.c_quote = c_quote
        self.c_open = c_open
        self.c_close = c_close
        self.re_literal = re.compile("[^ \t\\\\{0}]+".format(re.escape(
            c_line_comment + c_item_sep + c_kv_sep + c_quote + c_open + c_close)))

    def seal(self):
        if self.sealed:
            return
        self.sealed = True
        self.state_stack = [ ]
        self.s_aux_stack = [ ]
        self.value_stack = [ ]
        self.buf = ""

    def state_get(self):
        if len(self.state_stack) == 0:
            raise ParseException("stack is empty")
        return self.state_stack[len(self.state_stack) - 1]

    def state_set(self, state):
        if len(self.state_stack) == 0:
            raise ParseException("stack is empty")
        self.state_stack[len(self.state_stack) - 1] = state

    def aux_get(self):
        if len(self.s_aux_stack) == 0:
            raise ParseException("stack is empty")
        return self.s_aux_stack[len(self.s_aux_stack) - 1]

    def aux_set(self, aux):
        if len(self.s_aux_stack) == 0:
            raise ParseException("stack is empty")
        self.s_aux_stack[len(self.s_aux_stack) - 1] = aux

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
        self.s_aux_stack.pop()
        self.value_stack.pop()

    def push(self, state, aux, value):
        self.state_stack.append(state)
        self.s_aux_stack.append(aux)
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
        value = self.value_get()
        if start_c == 'n':
            value.o = self.op.append_string_byte(value.o, ord('\n'))
            self.buf_read_pos += 1
        elif start_c == 't':
            value.o = self.op.append_string_byte(value.o, ord('\t'))
            self.buf_read_pos += 1
        elif start_c == 'b':
            value.o = self.op.append_string_byte(value.o, ord('\b'))
            self.buf_read_pos += 1
        elif start_c == 'f':
            value.o = self.op.append_string_byte(value.o, ord('\f'))
            self.buf_read_pos += 1
        elif start_c == 'x':
            if self.buf_read_pos + 2 >= len(self.buf):
                raise ParseException("expect 2 hex chars for utf-8 escaping")
            code_str = self.buf[self.buf_read_pos + 1:self.buf_read_pos + 3]
            try:
                code = int(code_str, 16)
            except Exception:
                raise ParseException(
                    "expect 2 hex chars for utf-8 escaping (parse {0} failed)"
                    .format(code_str))
            value.o = self.op.append_string_byte(value.o, code)
            self.buf_read_pos += 3
        else:
            value.o = self.op.append_string_byte(value.o, ord(start_c))
            self.buf_read_pos += 1

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
                        self.push(self.STATE_ELEMENT_START, None, None)

                    continue

                state = self.state_get()
                current = self.value_get()

                if state == self.STATE_ALIST:
                    if current.has_tmp:
                        current.o = self.op.append_item(current.o, current.tmp)
                    current.has_tmp = True
                    current.tmp = value.o
                    current.is_string = value.is_string
                    current.is_literal = value.is_literal
                elif state == self.STATE_ALIST_WITH_KEY:
                    current.o = self.op.append_kv(
                        current.o, current.tmp, current.is_literal, value.o)
                    current.has_tmp = False
                    current.tmp = None
                    current.is_string = False
                    current.is_literal = False
                    state = self.STATE_ALIST
                else:
                    raise ParseException("invalid position to insert element")

            elif state == self.STATE_QUOTED_STRING:
                p = read_pos
                q = self.c_quote[self.aux_get()]
                while p < len(self.buf) and self.buf[p] != "\\" and self.buf[p] != q:
                    p = p + 1

                if p >= len(self.buf):
                    current.o = self.op.append_string_bytearray(
                        current.o, self.buf[read_pos:].encode("utf-8"))
                    current.o = self.op.finalize_string(current.o)
                    state = self.STATE_ELEMENT_END
                    read_pos = len(self.buf)
                else:
                    current.o = self.op.append_string_bytearray(
                        current.o, self.buf[read_pos:p].encode("utf-8"))
                    if self.buf[p] == "\\":
                        self.buf_read_pos = p + 1
                        self.handle_escape()
                        continue
                    elif self.buf[p] == q:
                        current.o = self.op.finalize_string(current.o)
                        read_pos = p + 1
                        state = self.STATE_ELEMENT_END
                    else:
                        raise ParseException("format error in quoted string")

            elif state == self.STATE_MULTILINE_STRING:
                p = read_pos
                q = self.c_quote[self.aux_get()]
                while p < len(self.buf) and self.buf[p] != "\\" and self.buf[p] != q:
                    p = p + 1

                if p >= len(self.buf):
                    current.o = self.op.append_string_bytearray(
                        current.o, self.buf[read_pos:].encode("utf-8"))
                    current.o = self.op.append_string_byte(current.o, ord("\n"))
                    read_pos = len(self.buf)
                else:
                    current.o = self.op.append_string_bytearray(
                        current.o, self.buf[read_pos:p].encode("utf-8"))
                    if self.buf[p] == "\\":
                        self.buf_read_pos = p + 1
                        self.handle_escape()
                        continue
                    elif self.buf[p] == q and self.buf[p + 1] == q and self.buf[p + 2] == q:
                        current.o = self.op.finalize_string(current.o)
                        read_pos = p + 3
                        state = self.STATE_ELEMENT_END
                    else:
                        raise ParseException("format error in multi-line string")

            elif state == self.STATE_ALIST:
                p = read_pos
                q = self.c_close[self.aux_get()]
                while p < len(self.buf) and (self.buf[p] == " " or self.buf[p] == "\t"):
                    p = p + 1

                if p >= len(self.buf):
                    read_pos = len(self.buf)
                elif self.buf[p] == q:
                    if current.has_tmp:
                        current.has_tmp = False
                        current.o = self.op.append_item(current.o, current.tmp)
                        current.tmp = None
                        current.is_string = False
                        current.is_literal = False
                    current.o = self.op.finalize_alist(current.o)
                    state = self.STATE_ELEMENT_END
                    read_pos = p + 1
                elif self.buf[p] in self.c_kv_sep:
                    if not current.has_tmp:
                        raise ParseException("missing key")
                    elif not current.is_string and not current.is_literal:
                        raise ParseException("key is not a string")
                    else:
                        state = self.STATE_ALIST_WITH_KEY
                        read_pos = p + 1
                elif self.buf[p] in self.c_item_sep:
                    self.buf_read_pos = p + 1
                    self.push(self.STATE_ELEMENT_START, None, None)
                    continue
                elif self.buf[p] in self.c_line_comment:
                    read_pos = len(self.buf)
                else:
                    self.buf_read_pos = p
                    self.push(self.STATE_ELEMENT_START, None, None)
                    continue

            elif state == self.STATE_ALIST_WITH_KEY:
                p = read_pos
                while p < len(self.buf) and (self.buf[p] == " " or self.buf[p] == "\t"):
                    p = p + 1

                if p >= len(self.buf):
                    read_pos = len(self.buf)
                elif self.buf[p] in self.c_line_comment:
                    read_pos = len(self.buf)
                else:
                    self.buf_read_pos = p
                    self.push(self.STATE_ELEMENT_START, None, None)
                    continue

            elif state == self.STATE_ELEMENT_START:
                p = read_pos
                while p < len(self.buf) and (self.buf[p] == " " or self.buf[p] == "\t"):
                    p = p + 1

                if p >= len(self.buf):
                    read_pos = len(self.buf)
                elif self.buf[p] in self.c_open:
                    current = AListInternalValue()
                    current.has_tmp = False
                    current.o = self.op.new_alist()
                    state = self.STATE_ALIST
                    self.aux_set(self.c_open.find(self.buf[p]))
                    read_pos = p + 1
                elif self.buf[p] in self.c_quote:
                    current = AListInternalValue()
                    current.has_tmp = False
                    current.tmp = None
                    current.o = self.op.new_string()
                    current.is_string = True
                    current.is_literal = False
                    self.aux_set(self.c_quote.find(self.buf[p]))

                    if self.buf[p + 1] == self.buf[p] and self.buf[p + 2] == self.buf[p]:
                        state = self.STATE_MULTILINE_STRING
                        read_pos = p + 3
                    else:
                        state = self.STATE_QUOTED_STRING
                        read_pos = p + 1
                elif self.buf[p] in self.c_line_comment:
                    read_pos = len(self.buf)
                else:
                    current = AListInternalValue()
                    current.has_tmp = False
                    current.tmp = None
                    current.is_string = False
                    current.is_literal = True

                    m = self.re_literal.match(self.buf, p)
                    if not m or m.end(0) == p:
                        raise ParseException("format error in parsing general element")
                    else:
                        current.o = self.op.new_literal(self.buf[p:m.end(0)])
                        state = self.STATE_ELEMENT_END
                        read_pos = m.end(0)

            self.state_set(state)
            self.value_set(current)
            self.buf_read_pos = read_pos

        self.clean_buf()

    def extract(self):
        if len(self.values) == 0:
            return None
        else:
            return self.values.popleft().o
