
# Benchmarking data APIs 

To start postgres `./launch-postgres.sh start`

To start benchmark:
```
./build/gbench --benchmark_format=csv --tuples=1000000 --benchmark_time_unit=ms 
```