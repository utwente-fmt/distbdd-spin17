# finding small models (transition relation smaller than 10MB):
find . -type f -size -10M -name "*.bdd"

# finding larger models (transition relation larger than 10MB):
find . -type f -size +10M -name "*.bdd"