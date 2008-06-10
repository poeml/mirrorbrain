
import ConfigParser

conffile = '/etc/mirrorbrain.conf'

cp = ConfigParser.SafeConfigParser()
cp.read(conffile)
config = dict(cp.items('general'))

