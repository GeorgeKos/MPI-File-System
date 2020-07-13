build:
	@echo "Building main..."
	mpiCC -std=c++14 -Wall -Wextra -pedantic main.cpp -o mpi.out
buildold:
	@echo "Building old main..."
	mpiCC -std=c++14 -Wall -Wextra -pedantic mainOld.cpp -o mpi.out
run:
	@echo "Running.."
	mpirun -np 14 --host kitro,frapa,ananas mpi.out 
run_14:
	@echo "Running.."
	mpirun -np 14 --host kitro,frapa,ananas mpi.out 10 numservers10_np14.txt
run_35:
	@echo "Running.."
	mpirun -np 35 --host kitro,frapa,ananas mpi.out 15 numservers15_np35.txt
run_70:
	@echo "Running.."
	mpirun -np 70 --host kitro,frapa,ananas mpi.out 30 numservers30_np70.txt
clean:
	@echo "Cleaning executables"
	rm -rf *.out