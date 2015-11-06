#include "srv.h"
#include <string.h>


/*
 *  Ejemplo de servidor que tiene el "sí fácil" para con su
 *  cliente y no se lleva bien con los demás servidores.
 *
 */

#define TAG_REQUEST 60
#define TAG_REPLY   70
#define TAG_DEAD    80

#define false 0
#define true 1
#define FALSE 0
#define TRUE 1
typedef char bool;

void servidor(int mi_cliente) {
    int our_seq = 0;
    int highest_seq = 0;
    int expecting_reply_count = 0;
    bool requesting_critical = false;

    // Calculamos nuestro numero de rank
    int yo = mi_cliente - 1;

    // Calculamos la cantidad de servidores inicial en la red
    int servers = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &servers);
    servers /= 2;

    // Inicializamos el arreglo de reply deferred
    bool *deferred = malloc(sizeof(bool) * servers);
    memset(deferred, 0, sizeof(bool) * servers);

    // Inicializamos el arreglo de esperando respuestas
    bool *waiting = malloc(sizeof(bool) * servers);
    memset(waiting, 0, sizeof(bool) * servers);

    // Inicializamos el arreglo de servidores muertos
    bool *dead = malloc(sizeof(bool) * servers);
    memset(dead, 0, sizeof(bool) * servers);

    // Cantidad de servidores vivos
    int alive = servers;

    MPI_Status status;
    int origen;
    int tag;
    int listo_para_salir = FALSE;

    int i;

    debug("Datos inicializados");
    
    while(listo_para_salir == FALSE) {
        int parameter;
        MPI_Recv(&parameter, 1, MPI_INT, ANY_SOURCE, ANY_TAG, COMM_WORLD, &status);

        origen = status.MPI_SOURCE;
        tag = status.MPI_TAG;
        
        if (tag == TAG_PEDIDO) {
            // Sólo podriamos recibir un mensaje de pedido por parte de nuestro cliente
            assert(origen == mi_cliente);
            debug("Mi cliente solicita acceso exclusivo");

            // No podría ya haber pedido la sección crítica, debería estar bloqueado, esperando
            assert(requesting_critical == FALSE);
            requesting_critical = TRUE;

            debug("Dándole permiso");

            // Este es el número de secuencia que vamos a utilizar
            our_seq = highest_seq + 1;

            if (alive == 1) {
                // Soy el único servidor, así que no me hago drama y le doy el recurso
                MPI_Send(NULL, 0, MPI_INT, mi_cliente, TAG_OTORGADO, COMM_WORLD);
                debug("Ultimo servidor en pie, otorgando recurso");
            } else {
                for (i = 0; i < servers; ++i) {
                    int rank = 2*i;

                    // Les pido a todos los que estén vivos que me den permiso, excepto a mí mismo
                    if (dead[i] == FALSE && rank != yo) {
                        MPI_Send(&our_seq, 1, MPI_INT, rank, TAG_REQUEST, COMM_WORLD);
                        waiting[i] = TRUE;
                    }
                }

                // Reseteo el conteo de espera de respuestas
                expecting_reply_count = alive - 1;
                debug("REQUESTs enviados, esperando REPLYs");
            }
        } else if (tag == TAG_LIBERO) {
            // Sólo podriamos recibir un mensaje de libero por parte de nuestro cliente
            assert(origen == mi_cliente);
            debug("Mi cliente libera su acceso exclusivo");

            // Sólo puede mandar este mensaje si ya tiene el mutex
            assert(requesting_critical == TRUE);
            requesting_critical = FALSE;

            // Le mandamos a los servidores que hayamos deferreado que ya pueden continuar sin nosotros
            for (i = 0; i < servers; ++i) {
                if (dead[i] == FALSE && deferred[i] == TRUE) {
                    int rank = 2*i;
                    deferred[i] = FALSE;
                    MPI_Send(NULL, 0, MPI_INT, rank, TAG_REPLY, COMM_WORLD);
                }
            }
        } else if (tag == TAG_REQUEST) {
            // Sólo puede enviarnos este mensaje un servidor
            assert((origen % 2) == 0);
            debug("Recibido un request");

            // Siempre hay que actualizar el numero de secuencia maxima
            if (parameter > highest_seq) {
                highest_seq = parameter;
            }

            if (dead[origen/2] == FALSE) {
                // Si nosotros estamos pidiendo el recurso
                if (requesting_critical == TRUE) {
                    // Si el otro nodo lo pidió primero
                    if (parameter > our_seq) {
                        // Nosotros pedimos antes, deferreamos su pedido
                        deferred[origen/2] = TRUE;
                    } else if (parameter == our_seq) {
                        // Lo pedimos "al mismo tiempo", ponemos prioridad sobre los que tengan números más grandes
                        if (origen > yo) {
                            MPI_Send(NULL, 0, MPI_INT, origen, TAG_REPLY, COMM_WORLD);
                        } else {
                            deferred[origen/2] = TRUE;
                        }
                    } else {
                        // Lo pidió él antes que yo
                        MPI_Send(NULL, 0, MPI_INT, origen, TAG_REPLY, COMM_WORLD);
                    }
                } else {
                    // Como no estamos pidiendo nada, basta con mandarle reply
                    MPI_Send(NULL, 0, MPI_INT, origen, TAG_REPLY, COMM_WORLD);
                }
            }
        } else if (tag == TAG_REPLY) {
            // Sólo puede enviarnos este mensaje un servidor
            assert((origen % 2) == 0);
            assert(requesting_critical == TRUE);
            debug("Recibido un reply");

            // Si no está muerto
            if (dead[origen/2] == FALSE) {
                // Marcamos que recibimos la respuesta
                expecting_reply_count--;
                waiting[origen/2] = FALSE;

                // Si era el último que faltaba, avisamos al cliente que ya tiene permiso
                if (expecting_reply_count == 0) {
                    MPI_Send(NULL, 0, MPI_INT, mi_cliente, TAG_OTORGADO, COMM_WORLD);
                }
            }
        } else if (tag == TAG_DEAD) {
            // Sólo puede enviarnos este mensaje un servidor
            assert((origen % 2) == 0);
            debug("Recibido un dead");

            // Lo marcamos como muerto
            dead[origen/2] = TRUE;
            alive--;

            // Si estabamos esperando una respuesta del server
            if (requesting_critical == TRUE && waiting[origen/2] == TRUE) {
                // No lo esperamos más, y hacemos como si esto fuera su respuesta
                waiting[origen/2] = FALSE;
                expecting_reply_count--;

                // Si justo el que murió es el último que faltaba, avisamos al cliente que ya tiene permiso
                if (expecting_reply_count == 0) {
                    MPI_Send(NULL, 0, MPI_INT, mi_cliente, TAG_OTORGADO, COMM_WORLD);
                }
            }
        } else if (tag == TAG_TERMINE) {
            assert(origen == mi_cliente);
            debug("Mi cliente avisa que terminó");
            listo_para_salir = TRUE;

            for (i = 0; i < servers; ++i) {
                int rank = 2*i;

                if (dead[i] == FALSE && rank != yo) {
                    MPI_Send(NULL, 0, MPI_INT, rank, TAG_DEAD, COMM_WORLD);
                }
            }
        }
        
    }
    
    // Liberamos memoria
    free(deferred);
    free(waiting);
    free(dead);
}

