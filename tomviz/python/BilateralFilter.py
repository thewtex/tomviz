import tomviz.operators


class BilateralFilter(tomviz.operators.CancelableOperator):

    def transform_scalars(self, dataset, domain_sigma=2.0, range_sigma=20.0):
        """Performs smoothing in both the spatial domain and intensity range.
        """

        # Initial progress
        self.progress.value = 0
        self.progress.maximum = 100

        # Approximate percentage of work completed after each step in the
        # transform
        STEP_PCT = [10, 20, 90, 100]

        try:
            import itk
            import itkTypes
            from tomviz import itkutils
        except Exception as exc:
            print("Could not import necessary module(s)")
            raise exc

        try:
            self.progress.value = STEP_PCT[0]
            self.progress.message = "Converting data to ITK image"

            itk_image = itkutils.convert_vtk_to_itk_image(dataset)
            itk_image_type = type(itk_image)

            self.progress.value = STEP_PCT[1]
            self.progress.message = "Running filter"

            smoothing_filter = \
                itk.BilateralImageFilter[itk_image_type, itk_image_type].New()
            smoothing_filter.SetDomainSigma(domain_sigma)
            smoothing_filter.SetRangeSigma(range_sigma)
            smoothing_filter.SetInput(itk_image)
            # itkutils.observe_filter_progress(self, smoothing_filter,
                                             # STEP_PCT[1], STEP_PCT[2])

            try:
                smoothing_filter.Update()
            except RuntimeError:
                return

            self.progress.message = "Saving results"

            itkutils.set_array_from_itk_image(dataset,
                                              smoothing_filter.GetOutput())

            self.progress.value = STEP_PCT[3]
        except Exception as exc:
            print("Problem encountered while running %s" %
                  self.__class__.__name__)
            raise exc
