#!/bin/bash

mkdir out
rm out/*
cp ../build/cfg_* out/

cd out/

for i in {0..7}; do
	REPETION=$(( ($i >> 0) & 1 ))
	FAST=$((($i >> 1) & 1))
	SINGLE_THREAD=$((($i >> 2) & 1))

	filename="cfg_STRINGS_$i"
	og_filename="${filename}"
	if [ $REPETION -eq 1 ]; then filename="${filename}_REPETION"; else filename="${filename}_NOREP"; fi
	if [ $FAST -eq 1 ]; then filename="${filename}_FAST"; else filename="${filename}_QUEUE"; fi
	if [ $SINGLE_THREAD -eq 1 ]; then filename="${filename}_SINGLETHREAD"; else filename="${filename}_MULTITHREAD"; fi

	valgrind --tool=massif --massif-out-file="$filename.massif" "./${og_filename}" 0
	for j in {0..4}; do
		{ time "./${og_filename}" 0; } 2> "$filename.time$j"
	done
	./$og_filename 1 > "$filename.out"
done

for i in {0..31}; do
	REPETION=$((($i >> 0) & 1))
	FAST=$((($i >> 1) & 1))
	SINGLE_THREAD=$((($i >> 2) & 1))
	LOW_MEM=$((($i >> 3) & 1))
	DERIVATION_FQ=$((($i >> 4) & 1))

	filename="cfg_DERIVATIONS_$i"
	og_filename="$filename"
	if [ $REPETION -eq 1 ]; then filename="${filename}_REPETION"; else filename="${filename}_NOREP"; fi
	if [ $FAST -eq 1 ]; then filename="${filename}_FAST"; else filename="${filename}_QUEUE"; fi
	if [ $SINGLE_THREAD -eq 1 ]; then filename="${filename}_SINGLETHREAD"; else filename="${filename}_MULTITHREAD"; fi
	if [ $LOW_MEM -eq 1 ]; then filename="${filename}_LOWMEM"; else filename="${filename}_HIGHMEM"; fi
	if [ $DERIVATION_FQ -eq 1 ]; then filename="${filename}_DFQ"; else filename="${filename}_DCQ"; fi

	valgrind --tool=massif --massif-out-file="$filename.massif" "./${og_filename}" 0
	for j in {0..4}; do
		{ time "./${og_filename}" 0; } 2> "$filename.time$j"
	done
	./$og_filename 1 > "$filename.out"
done

cd ..
echo "Running python script..."
./to_spreadcheet.py
