import sys, os


def dgst(file):
    import md5
    BUFSIZE = 1024*1024
    s = md5.new()
    f = open(file, 'r')
    while 1:
        buf = f.read(BUFSIZE)
        if not buf: break
        s.update(buf)
    return s.hexdigest()
    f.close()


def edit_file(data):
    import tempfile, difflib

    #delim = '--This line, and those below, will be ignored--\n\n'
    boilerplate = """\nNote: You cannot modify 'identifier' or 'id'.\n\n"""
    data = boilerplate + data

    (fd, filename) = tempfile.mkstemp(prefix = 'mb-editmirror', suffix = '.txt', dir = '/tmp')
    f = os.fdopen(fd, 'w')
    f.write(data)
    #f.write('\n')
    #f.write(delim)
    f.close()
    hash_orig = dgst(filename)

    editor = os.getenv('EDITOR', default='vim')
    while 1:
        os.system('%s %s' % (editor, filename))
        hash = dgst(filename)

        if hash == hash_orig:
            sys.stdout.write('No changes.\n')
            os.unlink(filename)
            return 
        else:
            new = open(filename).read()
            #new = new.split(delim)[0].rstrip()

            differ = difflib.Differ()
            d = list(differ.compare(data.splitlines(1), new.splitlines(1)))
            d = [ line for line in d if not line.startswith('?') ] 
            sys.stdout.writelines(d)
            sys.stdout.write('\n\n')

            input = raw_input('Save changes?\n'
                              'y)es, n)o, e)dit again: ')
            if input in 'yY':
                os.unlink(filename)
                return new
            elif input in 'nN':
                os.unlink(filename)
                return
            else:
                pass
