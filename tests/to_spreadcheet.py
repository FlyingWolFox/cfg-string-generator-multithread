#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import csv
import re
from pathlib import Path

peak_re = re.compile(r'mem_heap_B=(\d+)[^#]*?mem_heap_extra_B=(\d+)(?=[^#]*heap_tree=peak)', re.DOTALL)
time_re = re.compile(r'(\d)*m(\d*,\d*)s')

def get_peakmem(massif_out):
	match = peak_re.search(massif_out)
	return int(match.group(1)) + int(match.group(2))


def get_time(time_out):
	return [float(x[0]) * 60 + float(x[1].replace(',', '.')) for x in time_re.findall(time_out)]


def add_data(master_key, key, value, data, time=False):
	if master_key in data:
		if time:
			if key in data[master_key]:
				data[master_key][key].append(value)
			else:
				data[master_key][key] = [value]
		else:
			data[master_key][key] = value
	else:
		if time:
			data[master_key] = {key: [value]}
		else:
			data[master_key] = {key: value}


data = {}
for f in Path('./out').iterdir():
	if f.suffix == '.massif':
		with f.open() as massif_out:
			add_data(f.stem, 'peak mem', get_peakmem(massif_out.read()), data)
	elif 'time' in f.suffix:
		with f.open() as time_out:
			times = get_time(time_out.read())
			add_data(f.stem, 'real', times[0], data, True)
			add_data(f.stem, 'user', times[1], data, True)
			add_data(f.stem, 'sys', times[2], data, True)

csvfile = open('results.csv', 'w', newline='')
header = ['number', 'mode', 'rep', 'fast', 'thrd', 'low', 'dfq', 'real', 'user', 'sys', 'peak mem', 'kiB', 'MiB', 'GiB']
spcheat = csv.DictWriter(csvfile, header)
spcheat.writeheader()
for k, v in data.items():
	row = {'mode': 'strings' if 'STRINGS' in k else 'derivations'}
	num = int(re.search(r'\d+', k).group())
	row.update({'number': num})
	[row.update({header[i + 2]: ( (num >> i) & 1 ) == 1}) for i in range(5)]
	v['real'] = sum(v['real']) / len(v['real'])
	v['user'] = sum(v['user']) / len(v['user'])
	v['sys'] = sum(v['sys']) / len(v['sys'])
	row.update(v)
	mem = v['peak mem']
	[row.update({header[i + 11]: mem/(1024**(i+1))}) for i in range(3)]

	spcheat.writerow(row)

csvfile.close()


print('done!')
