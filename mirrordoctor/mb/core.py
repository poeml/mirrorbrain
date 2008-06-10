class Directory:
    def __init__(self, name):
        self.name = name
        self.files = []

    def __str__(self):
        #return '%s:\n%s' % (self.name, '\n'.join(self.files))
        return '%-45s: %6s files' % (self.name, int(len(self.files)))

