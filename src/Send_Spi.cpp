#include "Send_Spi.h"
#include <SPI.h>

void sendSPI(int* AttX,int nbMax)       //int* AttX pour passage tableau par pointeur
{   
  int val = 0;
  for (int attn = 0; attn < 8; attn++)
  {
    //Serial.print(attn);
    //Serial.print(" ");
    //Serial.println(AttX[attn]);
    int z = (AttX[attn]/10);              // divise atténuateur par 10
    
    if (z < 63){
      if (z < 31){
        val = z & 31;
      }
      else{
        val = ((z - 31) & 31) + 32;
      }
    }
    else{
      val = ((z - 62) & 31) + 96;
    }

    val = val << 1;
    int pointCinq = z*10;                 // recherche le reste après multiplication
    if ((AttX[attn] - pointCinq) != 0)    // vrai si terminé par 5
    {  
      val |= (1 << 0);
    }
    //Serial.println(val);
    SPI.transfer(val);
  }
  
}