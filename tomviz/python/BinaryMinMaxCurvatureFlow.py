import tomviz.operators


class BinaryMinMaxCurvatureFlow(tomviz.operators.CancelableOperator):

    def transform_scalars(self, dataset, stencil_radius=2, iterations=10,
                          threshold=50.0):
        """This filter smooths a binary image by evolving a level set with a
        curvature-based speed function. The Stencil Radius determines the scale
        of the noise to remove. The Threshold determines the iso-contour
        brightness to discriminate between two pixel classes.
        """

        # Initial progress
        self.progress.value = 0
        self.progress.maximum = 100

        # Approximate percentage of work completed after each step in the
        # transform
        STEP_PCT = [10, 20, 70, 90, 100]

        try:
            import itk
            import itkExtras
            import itkTypes
            import vtk
            from tomviz import itkutils
            from tomviz import utils
        except Exception as exc:
            print("Could not import necessary module(s)")
            raise exc

        # Return values
        returnValues = None

        # Add a try/except around the ITK portion. ITK exceptions are
        # passed up to the Python layer, so we can at least report what
        # went wrong with the script, e.g,, unsupported image type.
        try:
            self.progress.value = STEP_PCT[0]
            self.progress.message = "Converting data to ITK image"

            # Get the ITK image
            itk_input_image_type = itkutils._get_itk_image_type(dataset)
            itk_image = itkutils.convert_vtk_to_itk_image(dataset, itkTypes.F)
            print(itk_image)
            itk_filter_image_type = type(itk_image)
            print(itk_filter_image_type)

            smoothing_filter = itk.BinaryMinMaxCurvatureFlowImageFilter[
                itk_filter_image_type, itk_filter_image_type].New()
            smoothing_filter.SetThreshold(threshold)
            smoothing_filter.SetStencilRadius(stencil_radius)
            smoothing_filter.SetNumberOfIterations(iterations)
            smoothing_filter.SetTimeStep(0.0625)
            smoothing_filter.SetInput(itk_image)
            itkutils.observe_filter_progress(self, smoothing_filter,
                                             STEP_PCT[1], STEP_PCT[2])
            print(smoothing_filter)

            try:
                print('updating')
                smoothing_filter.Update()
            except RuntimeError:
                return

            itk_image_data = smoothing_filter.GetOutput()
            print('smoothed: ', itk_image_data)

            # Cast output to the input type
            py_buffer_type = itk_input_image_type
            self.progress.message = "Casting output to input type"

            caster = itk.CastImageFilter[itk_filter_image_type,
                                         itk_input_image_type].New()
            caster.SetInput(itk_image_data)
            itkutils.observe_filter_progress(self, caster,
                                             STEP_PCT[2], STEP_PCT[3])

            try:
                caster.Update()
            except RuntimeError:
                return

            itk_image_data = caster.GetOutput()

            self.progress.value = STEP_PCT[3]
            self.progress.message = "Saving results"

            itkutils.set_array_from_itk_image(dataset,
                                              itk_image_data)

            self.progress.value = STEP_PCT[4]

        except Exception as exc:
            print("Problem encountered while running %s" %
                  self.__class__.__name__)
            raise exc
