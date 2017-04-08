class LineCommentExtractor:
    def __init__(self, comment_re, inner_parser):
        self.comment_re = comment_re
        self.inner_parser = inner_parser

    def parse(self, s):
        self.parse_lines(s.splitlines())

    def parse_lines(self, lines):
        for line in lines:
            self.parse_line(line)
        pass

    def parse_line(self, line):
        m =  self.comment_re.search(line)
        if m:
            self.inner_parser.parse_line(line[m.end(0):])

    def extract(self):
        return self.inner_parser.extract()

class LineCommentExtractorPlus:
    def __init__(self, inner_parser, line, multiline_start, multiline_end, escape_start, escape_end):
        self.inner_parser = inner_parser
        self.line = line
        self.multiline_start = multiline_start
        self.multiline_end = multiline_end
        self.escape_start = escape_start
        self.escape_end = escape_end
        self.multiline_level = 0
        self.escape_level = 0

    def parse(self, s):
        self.parse_lines(s.splitlines())

    def parse_lines(self, lines):
        for line in lines:
            self.parse_line(line)
        pass

    def parse_line(self, line):
        if self.escape_start.search(line):
            self.escape_level += 1
            return
        elif self.escape_end.search(line):
            self.escape_level -= 1
            return

        if self.escape_level > 0:
            return

        if self.multiline_start.search(line):
            self.multiline_level += 1
            return
        elif self.multiline_end.search(line):
            self.multiline_level -= 1
            return

        if self.multiline_level > 0:
            self.inner_parser.parse_line(line)
            return

        m = self.line.search(line)
        if m:
            self.inner_parser.parse_line(line[m.end(0):])

    def extract(self):
        return self.inner_parser.extract()
