/*
 * service.h — действия до основного цикла (калибровка по концевикам).
 */

#ifndef SERVICE_H_
#define SERVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

/** Калибровка MIN/MAX по концевикам; блокирует до завершения. См. service.c. */
void service_calibrate_limits(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_H_ */
