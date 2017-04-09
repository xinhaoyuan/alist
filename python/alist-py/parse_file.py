from .alist import ParseException

def parse_file(f, parser):
    line_num = 1
    for line in f:
        try:
            parser.parse_line(line.rstrip("\r\n"))
        except ParseException as e:
            raise Exception("Parser exception at line {0}: {1}".format(line_num, str(e)))
        line_num += 1
    return parser.extract()
