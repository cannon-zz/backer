file=avi.dat

for vid in n p ; do
	for dns in h l ; do
		for spd in s e ; do
			options="-V${vid} -D${dns} -F${spd}"
			echo -n "Testing ${options} ... "
			if { bkrencode --inject-noise ${options} <${file} | bkrencode -u ${options} | cmp ${file} ; } ; then
				echo OK
			else
				echo
			fi
		done
	done
done
