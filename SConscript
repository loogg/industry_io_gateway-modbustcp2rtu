from building import *

src = Glob('*.c') + Glob('*.cpp')
cwd = GetCurrentDir()

CPPPATH = [cwd]
group = DefineGroup('', src, depend = [''], CPPPATH = CPPPATH)
objs = [group]
list = os.listdir(cwd)

for item in list:
    if os.path.isfile(os.path.join(cwd, item, 'SConscript')):
        objs = objs + SConscript(os.path.join(item, 'SConscript'))

Return('objs')
