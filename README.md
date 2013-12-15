cs352proxy
==========

Syntax: ./cs352proxy [config file]

I believe that hardest part of this project was properly managing the structs and link lists/hash tables. Initially, I intended to construct the peer list and link state records using linked list but had difficulty managing the linked list because they would set next to itself and thus creating a never ending loop. I tried for a day or two to try and solve the problem but was unable to and eventually turned to hash tables. It took a bit to learn the mechanics of the hash tables but once I did, it was manageable.

Even with the test proxy, we were not told exactly how the packets were sent so we could adapt to it so I ran two copies on one VM and another on the server VM.

I was able to complete part 2 to specifications but was unable to work the part 3 requirements. Working alone was both good and bad. A benefit is that the code is easy to decipher since I wrote it but unable to split the workload.