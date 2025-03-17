f=( 32 64 128 256 512 1024 2048 4096 8192 )
for x in ${f[@]}; do
	y=$(cat msg_sz_$x.txt | head -n 10 | tail -n 1 | cut -d ' ' -f 2 | cut -d '=' -f 2 | tr -d ',' | awk '{printf "%f Gbps", $0/1024}')
	echo "$x: $y"
done
